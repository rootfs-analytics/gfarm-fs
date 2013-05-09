
GFSD_DEFAULT_PREFIX="/"
GFSD_DEFAULT_HOSTNAME="`hostname -f`"
GFSD_DEFAULT_PORT="600"

if [ -f ./zbx_gfarm_local.inc ]; then
    . ./zbx_gfarm_local.inc
fi

parse_gfsd_conf () {
    arg="$1"

    # check format
    seps=`echo "$arg" | sed -s 's/[^:]//g'`
    argc=0
    if [ "::" = "$seps" ]; then
        argc=3
    fi
    if [ ":::" = "$seps" ]; then
        argc=4
    fi
    if [ $argc -ne 3 -a $argc -ne 4 ]; then
        return 1
    fi

    prefix=`echo $arg | cut -d':' -f 1`
    hostname=`echo $arg | cut -d':' -f 2`
    if [ $argc -eq 3 ]; then
        port=`echo $arg | cut -d':' -f 3`
        listen_address=""
    else
        listen_address=`echo $arg | cut -d':' -f 3`
        port=`echo $arg | cut -d':' -f 4`
    fi

    if [ -z "${prefix}" ]; then
        prefix="${GFSD_DEFAULT_PREFIX}"
    fi
    if [ -z "${hostname}" ]; then
        hostname="${GFSD_DEFAULT_HOSTNAME}"
    fi
    if [ -z "${port}" ]; then
        port="${GFSD_DEFAULT_PORT}"
    fi

    echo "${prefix} ${hostname} ${port} ${listen_address}"
    return 0
}

iterate_over_separeted_list () {
    com=$1; shift 1
    list=$1; shift 1
    echo $list | sed 's/%/\n/g' |\
    while read elm; do
        $com $elm $* || return 1
    done
    return $?
}

#
# sample:
# >> parse_gfsd_confs '::=/usr/local/:gfsd2.gfarm.co.jp:10600'
# / gfsd.gfarm.co.jp 600 0
# /usr/local/ gfsd2.gfarm.co.jp 10600 1
#
parse_gfsd_confs () {
    iterate_over_separeted_list parse_gfsd_conf "$1"
    return $?
}

read_gfmd_conf () {
    prefix=$1
    if [ -z "${prefix}" ]; then
        prefix="/"
    fi
    host="`sudo grep metadb_server_host ${prefix}/etc/gfmd.conf | awk '{print $2}'`"
    port="`sudo grep metadb_server_port ${prefix}/etc/gfmd.conf | awk '{print $2}'`"
    echo "${prefix} ${host} ${port}"
    return 0
}

read_gfmd_confs () {
    iterate_over_separeted_list read_gfmd_conf "$1"
    return $?
}

read_gfarm_pgsql_conf () {
    prefix=$1
    if [ -z "${prefix}" ]; then
        prefix="/"
    fi
    port="`sudo grep postgresql_server_port ${prefix}/etc/gfmd.conf | awk '{print $2}'`"
    db="`sudo grep postgresql_dbname ${prefix}/etc/gfmd.conf | awk '{print $2}'`"
    user="`sudo grep postgresql_user ${prefix}/etc/gfmd.conf | awk '{print $2}'`"
    pass="`sudo grep postgresql_password ${prefix}/etc/gfmd.conf | awk '{print $2}' | sed 's/^\"\(.*\)\"$/\1/g'`"
    echo "${prefix} ${port} ${db} ${user} ${pass}"
    return 0
}

read_gfarm_pgsql_confs () {
    iterate_over_separeted_list read_gfarm_pgsql_conf "$1"
    return $?
}

average_or_error () {
    sum=0
    c=0
    while read val; do
        if [ -z "${val}" ]; then
            continue
        fi
        if [ "${val}" -lt 0 ]; then
            break
        fi
        sum=`expr $sum + $val`
        c=`expr $c + 1`
    done
    if [ "$c" -eq 0 ] || ( [ ! -z "${val}" ] && [ "${val}" -lt 0 ] ) ; then
        res="-1"
    else
        res=`expr $sum / $c`
    fi
    echo $res
    return 0
}

psql_wrapper () {
    DB_HOST=$1; shift 1
    DB_PORT=$1; shift 1
    DB_NAME=$1; shift 1
    DB_USER=$1; shift 1
    DB_PASS=$1; shift 1
    SQL="$*"

    PGPASSWORD=${DB_PASS}
    export PGPASSWORD

    result=`echo ${SQL} |\
             psql -U ${DB_USER} -h ${DB_HOST} -p ${DB_PORT} ${DB_NAME} -t -w -f - `
    ret=$?
    echo "$result"
    return $ret
}

gfarm_pgsql_exec () {
    confs=$1; shift 1
    sql=$*
    read_gfarm_pgsql_confs $confs |\
    while read prefix port db user pass; do
        psql_wrapper localhost "${port}" "${db}" "${user}" "${pass}" "${sql}"
    done
    return 0;
}


