# Syslog facility
SYSLOG_FACILITY=${SYSLOG_FACILITY:-"local0"}

# Timeout interval of 'gf*' command.
TIMEOUT=${TIMEOUT:-12}

# Exit code of 'gfservice-timeout' when timeout occurs.
TIMEOUT_EXITCODE=124

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

#
# Check an authentication error occcurs at execution of gf* command.
#
# Arguments:
#    $1    a flie which records error messages of the gf* command.
#
# Return value.
#    Return 0 in case of authentication error.
#
check_auth_error()
{
    test -r "$1" || return 0
    grep 'libgfarm: <err> \[1000059\]' "$1" > /dev/null 2>&1
}
