#ifndef MPIO_ENGINE_DATA_INITIALIZER
#define  MPIO_ENGINE_DATA_INITIALIZER(mType, mStartLane, mEndLane, mHotplug, mGpioGroupId) \
        { .EngineType = mType, \
          .HotPluggable = mHotplug, \
          .StartLane = mStartLane, \
          .EndLane = mEndLane, \
          .GpioGroupId = mGpioGroupId, \
        }
#endif

#ifndef MPIO_PORT_DATA_INITIALIZER_PCIE
#define  MPIO_PORT_DATA_INITIALIZER_PCIE(mPortPresent, mDevAddress, mDevFunction, mHotplug, mMaxLinkSpeed, \
          mMaxLinkCap, mAspm, mAspmL1_1, mAspmL1_2, mClkPmSupport) \
        { \
          .PortPresent = mPortPresent, \
          .DeviceNumber = mDevAddress, \
          .FunctionNumber = mDevFunction, \
          .LinkSpeedCapability = mMaxLinkSpeed, \
          .LinkAspm = mAspm, \
          .LinkAspmL1_1 = mAspmL1_1, \
          .LinkAspmL1_2 = mAspmL1_2, \
          .LinkHotplug = mHotplug, \
          .MiscControls = { \
            .LinkSafeMode = mMaxLinkCap, \
            .ClkPmSupport = mClkPmSupport, \
            .TurnOffUnusedLanes = 1, \
          }, \
        }
#endif
