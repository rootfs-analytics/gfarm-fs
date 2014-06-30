SYSLOG_FACILITY=${SYSLOG_FACILITY:-"local0"}

#
# Output a syslog message of the priority 'err'.
#
# Arguments:
#    $@    A log message.
#
# Return value.
#    It always returns 0.
#
log_err()
{
    logger -p $SYSLOG_FACILITY.err -i -t `basename $0` "$@"
    return 0
}

#
# Output a syslog message of the priority 'warning'.
#
# Arguments:
#    $@    A log message.
#
# Return value.
#    It always returns 0.
#
log_warning()
{
    logger -p $SYSLOG_FACILITY.warning -i -t `basename $0` "$@"
    return 0
}

#
# Output a syslog message of the priority 'notice'.
#
# Arguments:
#    $@    A log message.
#
# Return value.
#    It always returns 0.
#
log_notice()
{
    logger -p $SYSLOG_FACILITY.notice -i -t `basename $0` "$@"
    return 0
}

#
# Output a syslog message of the priority 'info'.
#
# Arguments:
#    $@    A log message.
#
# Return value.
#    It always returns 0.
#
log_info()
{
    logger -p $SYSLOG_FACILITY.info -i -t `basename $0` "$@"
    return 0
}
