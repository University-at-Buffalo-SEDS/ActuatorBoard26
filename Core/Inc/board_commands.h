#pragma once
/* Local command IDs do not depend on a network stack. */
enum actuator_commands {
    CMD_IGNITER_ON = 7,
    CMD_RETRACT_PLUMBING = 8,
    CMD_NITROGEN_OPEN = 9,
    CMD_NITROUS_OPEN = 10,
    CMD_IGNITER_OFF = 11,
    CMD_NITROGEN_CLOSE = 12,
    CMD_NITROUS_CLOSE = 13,
    CMD_IGNITER_SEQUENCE = 14,
};
