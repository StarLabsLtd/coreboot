project(
  'IntelFitTool',
  version: '1',
  license: 'GPL2',
  meson_version: '>= 0.52.0',
)

coreboot_dir = join_paths(meson.current_source_dir(), '../..')

fit = join_paths(get_option('stitch_dir'), 'fit')
meu = join_paths(get_option('stitch_dir'), 'meu')
signing_key = join_paths(get_option('stitch_dir'), 'private.pem')

truncate = find_program('truncate')

# Create an empty file to be used for OBB
obb_padding = configure_file(
  output: 'OBB_Padding.bin',
  command: [truncate, '-s 512', '@OUTPUT@']
)

meu_conf = configuration_data()
meu_conf.set('signing_key', signing_key)
meu_conf.set('coreboot_dir', coreboot_dir)

meu_config = configure_file(
  input: 'meu_config.xml.in',
  output: 'meu_config.xml',
  configuration: meu_conf,
)

# Create hash of blobs that need it
foreach blob: get_option('hashed_blobs')
  hashed = configure_file(
    output: '@0@@1@'.format('private_hash_', blob.split('.')[0]),
    command: [meu,
              '-cfg',
              meu_config,
              '-keyhash',
              '@OUTPUT@',
              '-f', 
              '@0@/@1@'.format(get_option('stitch_dir'),blob),]
  )
endforeach

key_hash = configure_file(
  output: 'private_hash',
  command: [meu,
            '-cfg',
            meu_config,
            '-keyhash',
            '@OUTPUT@',
            '-key',
            signing_key]
)

# This is temporary until we can make coreboot make a nice IBB
ibbl_padding = configure_file(
  output: 'Stitch_IBBM.bin',
  command: [truncate, '-s 512', '@OUTPUT@']
)


bios = configure_file(
  output: 'bios.bin',
  command: [meu,
            '-f',
            '../bios.xml',
            '-cfg',
             meu_config,
            '-o',
            '@OUTPUT@',
            '-key',
            signing_key]
)

# Create key manifest binary
key_manifest = configure_file(
  output: 'oemkeymn2.bin',
  command: [meu,
            '-f',
            '../OEMKeyManifest.xml',
            '-cfg',
            meu_config,
            '-o',
            '@OUTPUT@']
)

# Configure xml for stitching
hash = run_command(
  'cat', files('build/private_hash.txt'),
).stdout().strip()

spi_conf = configuration_data()
spi_conf.set('stitch_dir', get_option('stitch_dir'))
spi_conf.set('signing_key', signing_key)
spi_conf.set('coreboot_dir', coreboot_dir)
spi_conf.set('key_hash', hash)
if (get_option('processor') == 'apollolake')
  spi_conf.set('patch1', '3rdparty/intel-microcode/intel-ucode/06-5c-09')
  spi_conf.set('patch2', '3rdparty/intel-microcode/intel-ucode/06-5c-0a')
  spi_conf.set('sku', 'APL')
  spi_conf.set('region', '415')
else
  spi_conf.set('patch1', '3rdparty/intel-microcode/intel-ucode/06-7a-01')
  spi_conf.set('patch2', '3rdparty/intel-microcode/intel-ucode/06-7a-08')
  spi_conf.set('sku', 'GLK')
  spi_conf.set('region', '1548')
endif
if get_option('bootguard')
  spi_conf.set('bootguard', 'Boot Guard Profile 2 - VM')
else
  spi_conf.set('bootguard', 'Boot Guard Profile 0 - Legacy')
endif

spi_config = configure_file(
  input: 'spi.xml.in',
  output: 'spi.xml',
  configuration: spi_conf,
)

# Stitch together to create rom
coreboot_rom = configure_file(
  input: [
    obb_padding,
    hashed,
    ibbl_padding,
    bios,
    key_manifest,
],
  output: 'coreboot.rom',
  command: [fit,
            '-b',
            '-f',
            spi_config,
            '-o',
            '@OUTPUT@',
            '-st_path',
            '/usr/bin/openssl'],
)

