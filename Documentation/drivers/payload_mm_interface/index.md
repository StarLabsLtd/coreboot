# Payload MM

## Introduction

Payload MM is a new feature that enables a bootloader and payload to share the SMM environment.
The objective is to allow the payload to install feature-specific handlers, while keeping the
ownership, initialisation and silicon-specific components of SMM in the bootloader, where they
belong. An example use-case - and the primary one prepared for support at this time - is UEFI
secure boot with authenticated variables at runtime inside SMM.

A key objective of the payload MM design is to keep the bootloader and payload responsibilities
separate behind a small, versioned interface. Both projects require explicit integration, but the
runtime payload does not own coreboot's silicon-specific SMM setup.

> [**!CAUTION**]
> With regards to security, it is critical that payload MM only be enabled with a matched payload.
> The load command copies privileged code into SMRAM by design. coreboot therefore permits only
> one load attempt, consumes that attempt before validating caller data, and requires the payload
> to close the loader before OS handoff. Runtime dispatch remains available only after successful
> registration. These controls limit the loading phase; they do not authenticate the payload.

## Implementation considerations

There are several things that payload MM implementations have to take into account:

### Preserve hardware state

The payload should not make assumptions that CPU features are enabled or not. For example, if
FPU registers are required, the intermediate payload MM code must fxsave/fxrstor and enable
these before entering the actual payload.

### Calling conventions

The bootloader always calls payload MM in its native bitness (32-bit vs 64-bit, little-endian vs
big-endian), and in the System-V ABI. If the payload code's bitness is different, or if the
payload uses a different ABI, then it is the responsibility of payload MM 'glue' code to check
and perform a mode switch and/or ABI switch as needed.

Mode switches are easier to do at runtime than during initialisation, since the loaded module(s)
can check the coreboot table with the tag `LB_TAG_PLD_MM_INTERFACE_INFO` for the bootloader's
bitness and reference relevant functions as if they were variables. In contrast, during
initialisation, the loader might only be able to reference the MM core's entrypoint (via the
ELF/PE-COFF header).

A possible solution to this involves storing a custom struct in the data section of the MM
core, tagged with a magic value and referencing multiple possible entrypoints, scanning the
data section's memory in the loader module and then choosing which entrypoint to provide to
the bootloader in the `struct payload_mm_load_context`.

#### Discerning between x86-32 and x86-64

If one only needs to differentiate between x86-32 and x86-64, the easiest way involves using
one-byte `inc` opcodes that are repurposed as REX prefixes in 64-bit mode:

```
xor     eax,eax ; Clear ZF
db      0x40    ; 32-bit: inc eax   64-bit: REX prefix
nop             ; EAX is 1 if running in 32-bit mode
```

### Shared information

The first 4K of the payload MM subregion is reserved for the fixed data shared between coreboot
and payload MM. Neither side may allocate this page from its general heap.
- The first half of this contains a `struct payload_mm_shared_info`. Its purpose is to
  provide a runtime entrypoint to payload MM to the bootloader.
- The second half begins with the `struct payload_mm_core_call_context` for which coreboot passes
  MM a pointer on the first entry. This data must be in memory mapped by MM.

The definitions in `src/include/payload_mm_interface.h` are normative. The abbreviated definition
below documents the current revision, but implementations must use the source header rather than
copying this text as an independent ABI definition.

```C
/*
 * The data below is shared between the bootloader and payload MM in the shared memory, located
 * in the first half of the first 4K of the payload MM subregion. It must be provided by the
 * payload MM at runtime, as it describes the data required by the bootloader to call payload MM.
 */
#define PLD_MM_SHARED_STRUCT_MAGIC	0x5f4d4d5f444c505f  /* '_PLD_MM_' */
#define PLD_MM_SHARED_STRUCT_MAX_SIZE 	2048
#define PLD_MM_SHARED_STRUCT_REVISION	1

struct payload_mm_shared_info {
	uint64_t header_magic;
	uint16_t shared_info_size;
	uint8_t  header_revision;
	uint8_t  reserved;
	uint32_t mm_entrypoint_address;
} __packed;
```

## Hand-off

The bootloader hands off some data to describe the SMRAM regions and SMI interface it provides.

### `LB_TAG_PLD_MM_INTERFACE_INFO == 0x003b`

This contains the SWSMI number of the payload MM interface, the bootloader's own bitness, and
revision-specific platform data. Revision 1 adds the fixed Star Labs CFR mailbox address, size and
supported-option mask. The mailbox is an optional bounded consumer and is not part of the loader
command buffer.

```C
struct lb_payload_mm_interface_info {
	uint32_t tag;
	uint32_t size;
	uint8_t revision;			/* The version of this table. Currently "1" */
	uint8_t bootloader_smm_is_64bit;	/* Whether the bootloader's SMM is 64-bit code. This aids
						   the payload determine if mode switching is required. */
	uint8_t apm_cmd;			/* The command byte to write to the APM I/O port */
	uint8_t pad;
	lb_uint64_t cfr_mailbox;
	uint32_t cfr_mailbox_size;
	uint32_t cfr_supported_options;
};
```

### `LB_TAG_PAYLOAD_MM_SMRAM_REGION == 0x003c`

This describes the region within the payload MM reservation which the payload may use, and the
separate coreboot SMM handler range which the payload must not claim.

```C
struct lb_pld_mm_smram_descriptor {
	lb_uint64_t physical_start;	/* Physical address of the descriptor */
	lb_uint64_t physical_size;	/* Size of the described region */
};

struct lb_payload_mm_smram_region {
	uint32_t tag;
	uint32_t size;
	struct lb_pld_mm_smram_descriptor descriptor;	/* The payload MM subregion */
	struct lb_pld_mm_smram_descriptor handler;	/* The coreboot SMM handler */
};
```

### `LB_TAG_PAYLOAD_MM_SHARED_MEM == 0x003d`

This describes the 'region' within the payload MM subregion which the payload is directed to
reserve as the shared memory.

```C
struct lb_pld_mm_smram_descriptor {
	lb_uint64_t physical_start;	/* Physical address of the descriptor */
	lb_uint64_t physical_size;	/* Size of the described region */
};

struct lb_payload_mm_shared_mem {
	uint32_t tag;
	uint32_t size;
	struct lb_pld_mm_smram_descriptor comm_buffer;	/* The shared memory */
};
```

### `LB_TAG_PLD_SPI_FLASH_INFO == 0x003e`

This supplies the SPI controller location and the physical flash geometry used by the resident
variable service. Revision 1 provides the SMMSTORE base, size and logical block size. Revision 2
adds the absolute SMMSTORE offset in the SPI flash address space because the CPU mapping need not
be linear with the SPI BIOS region. The consumer must translate this offset relative to the base
of that controller region for region-relative transactions. The base is a physical flash mapping,
not a cached RAM mapping. coreboot validates that the complete region is within one boot-device
mapping and contains at least three aligned logical blocks before publishing it.

```C
enum lb_pld_efi_acpi_3_0_memory_types {
	PLD_EFI_ACPI_3_0_SYSTEM_MEMORY = 0,
	PLD_EFI_ACPI_3_0_SYSTEM_IO,
	PLD_EFI_ACPI_3_0_PCI_CONFIGURATION_SPACE,
};

struct lb_pld_generic_register {
	uint8_t address_space_id;	/* The address space where this register is found.
					   Follows the ACPI types */
	uint8_t register_bit_width;	/* The width of this register */
	uint8_t register_bit_offset;	/* The offset into this register to use */
	uint8_t reserved;
	lb_uint64_t address;		/* The address of this register. Exact location
					   depends on the address space */
	lb_uint64_t value;		/* An optional value to set in this register */
};

#define FLAGS_SPI_DISABLE_SMM_WRITE_PROTECT (1 << 0)
struct lb_pld_mm_spi_controller_info {
	uint32_t tag;
	uint32_t size;
	uint16_t revision;				/* The version of this table. Currently "2" */
	uint16_t flags;					/* A set of flags to describe this SPI controller, defined above */
	struct lb_pld_generic_register spi_address;	/* The address of the PCIe SPI controller, if present */
	lb_uint64_t store_base;
	uint32_t store_size;
	uint32_t block_size;
	uint32_t store_offset;
};
```

## SMI handler interface

The bootloader implements the following SMI handler interface. The structure and command constants
in `src/include/payload_mm_interface.h` are normative.
- The ABI follows the one that's also used in other coreboot drivers, with `ah` containing the
  sub-command, `rbx` containing a function argument, and `rax` containing the return value.
  - Commands either return `PAYLOAD_MM_RET_SUCCESS == 0` or `PAYLOAD_MM_RET_FAILURE == 1`.

### `LOAD_AND_CALL_CORE == 1`

`memcpy`'s the provided buffer into the payload region, and calls the provided entrypoint,
passing a `struct payload_mm_core_call_context *`, which is a subset of the load context. For
the purposes of consumer-specific extensibility, an `implementation_private_data` field is
present in the load context, that will be copied to the call context, conveying relevant
information from the unprivileged environment to the privileged one.

The provided buffer must be reachable by the bootloader. Therefore, it must be below 4 GiB when
the bootloader declares that its SMM is 32-bit code, per LB\_TAG\_PLD\_MM\_INTERFACE\_INFO.

The `struct payload_mm_core_call_context` must be in memory that will be mapped by MM. The
primary implementation of payload MM does not map the coreboot area, and for security reasons, no
implementation should. Therefore, this struct should be in the second half of the shared memory.

Input: A pointer to a `struct payload_mm_load_context`.

The command is accepted at most once. coreboot marks the load attempted before checking the
command or caller-owned request, snapshots the request before validation, rejects source overlap
with SMRAM, and requires the destination and entrypoint to fit entirely within the payload-owned
region. A failed or malformed request cannot reopen the loader.

### `CLOSE_LOADER == 2`

Closes the load interface without copying or executing an image. It reports success only when a
payload was already registered. A matched payload issues this command before handing control to
the OS and treats failure to register or close as a boot failure.

```C
/*
 * The data below describes a load request from the payload MM loader (ring0) to bootloader SMM,
 * and the arguments that the loader wants passed to the payload MM core module.
 */
#define PLD_MM_LOAD_CONTEXT_REVISION	1

struct payload_mm_load_context {
	uint16_t header_size;
	uint8_t  header_revision;
	uint8_t  reserved;
	uint64_t mm_core_source_address;
	uint32_t mm_core_destination_address;
	uint32_t mm_core_size;
	uint32_t mm_entrypoint_offset;
	uint64_t mm_entrypoint_arg1;
	uint64_t mm_entrypoint_arg2;
	uint64_t mm_entrypoint_arg3;
	uint64_t implementation_private_data;
} __packed;

/*
 * Used to communicate payload MM loader (ring0) arguments from bootloader SMM to payload MM.
 */
struct payload_mm_core_call_context {
	uint64_t mm_entrypoint_arg1;
	uint64_t mm_entrypoint_arg2;
	uint64_t mm_entrypoint_arg3;
	uint64_t implementation_private_data;
} __packed;
```

## Sequence

### Initialisation

The payload shall first prepare a buffer representing the MM core module outside of SMRAM, but
relocated (if necessary) to match its final destination. It then calls **LOAD_AND_CALL_CORE**.
Before returning, payload MM selects the runtime function and publishes its address in the shared
header. The payload then calls **CLOSE_LOADER** before OS handoff. The load path remains closed,
while successful registration permits normal runtime dispatch to the published entrypoint.

An example initialisation control flow (from payload MM IPL onwards) is below:

![Sequence_Init.png](./Sequence_Init.svg)

### Runtime

The bootloader calls the registered payload MM entrypoint for relevant software SMIs. The current
runtime entrypoint receives `NULL`; payload services use their fixed communication buffers rather
than caller-selected pointers. The Star Labs CFR mailbox is a separate fixed ACPI NVS interface
captured during MM initialization.

An example runtime control flow (from delivery of an APMC to the HW) is below:

![Sequence_Runtime.png](./Sequence_Runtime.svg)

### S3 resume

Payload MM and its shared header remain in SMRAM through S3, but coreboot reloads its SMM handler
and loses static registration state. On the first resumed invocation, coreboot closes the loader
unconditionally and restores registration only if the retained shared header has the expected
magic, size and revision and its entrypoint remains inside the payload-owned region. A missing or
malformed header leaves payload MM unregistered and cannot reopen loading from the resumed OS.

Payload MM rejects legacy SMMStore full-flash requests on S3 before the SMMStore capsule state
machine runs. This prevents a resumed OS from using reset SMMStore static state to enable or select
the full-flash region. Cold flash-update boots retain the legacy transport until the matched payload
processes the capsule and resets the machine.

## Addendum: Payload components

UefiPayload's component description will be updated with the matched EDK2 series once its
initialization design is finalized.

## Addendum: Notes and Open Issues

The [Star Labs preference interface](starlabs_preferences.md) is a bounded
ACPI consumer of the resident variable service, separate from the loader ABI.

**Open tasks**:
- Consider using the MM supervisor in UefiPayload (and directing payloads to strongly consider
  old and new security measures), this requires enhancing the supervisor first.

**Open questions**:
- How to avoid SWSMI number conflict?
  - In practice, this may not be an issue (yet). EDK2 uses `APM_CNT_NOOP_SMI` and its own communication buffers.
