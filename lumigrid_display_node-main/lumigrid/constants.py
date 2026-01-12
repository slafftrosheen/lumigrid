"""Shared constants for Lumigrid Display Node."""

# Control mode constants
CONTROL_MODE_EXTERNAL = 'external'
CONTROL_MODE_SYNC = 'sync'
CONTROL_MODE_INDEPENDENT = 'independent'

VALID_CONTROL_MODES = (
    CONTROL_MODE_EXTERNAL,
    CONTROL_MODE_SYNC,
    CONTROL_MODE_INDEPENDENT
)

# Sync role constants
SYNC_ROLE_MASTER = 'master'
SYNC_ROLE_SLAVE = 'slave'

VALID_SYNC_ROLES = (
    SYNC_ROLE_MASTER,
    SYNC_ROLE_SLAVE
)

# External source constants
EXTERNAL_SOURCE_MQTT = 'mqtt'
EXTERNAL_SOURCE_ARTNET = 'artnet'

VALID_EXTERNAL_SOURCES = (
    EXTERNAL_SOURCE_MQTT,
    EXTERNAL_SOURCE_ARTNET
)
