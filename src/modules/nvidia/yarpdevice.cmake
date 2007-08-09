

SET(YARPDEV_NAMES "nvidia")
SET(YARPDEV_WRAPPERS "None")
SET(YARPDEV_INCLUDES "NVIDIADeviceDriver.h")
SET(YARPDEV_TYPES "NVIDIAGPU")


IF (ENABLE_nvidia)

	LINK_LIBRARIES(pthread GL glut CgGL)
	ADD_DEFINITIONS(-DGL_GLEXT_PROTOTYPES -DGLX_GLXEXT_PROTOTYPES)
ENDIF (ENABLE_nvidia)
