#! /bin/sh

. ./zbx_gfarm_utils.inc

testGfsdConfsDefault () {
    read prefix gfsdhost port listen_address <<EOF
`parse_gfsd_confs "::"`
EOF
    assertEquals "failed to read" 0 $?
    assertEquals "prefix is not default!" "/" "${prefix}"
    assertEquals "hostname is not correct!" "`hostname -f`" "${gfsdhost}"
    assertEquals "port is not correct!" "600" "${port}"
    assertNotNull "listen address is not empty" ${listen_address}
}

testGfsdConfsDefault2 () {
    read prefix gfsdhost port listen_address <<EOF
`parse_gfsd_confs ":::"`
EOF
    assertEquals "failed to read" 0 $?
    assertEquals "prefix is not default!" "/" "${prefix}"
    assertEquals "hostname is not correct!" "`hostname -f`" "${gfsdhost}"
    assertEquals "port is not correct!" "600" "${port}"
    assertNotNull "listen address is not empty" ${listen_address}
}

testGfsdConfsPrefixSpecifiedFmt1 () {
    read prefix gfsdhost port listen_address <<EOF
`parse_gfsd_confs "/usr/local::"`
EOF
    assertEquals "failed to read" 0 $?
    assertEquals "prefix is not correct!" "/usr/local" "${prefix}"
    assertEquals "hostname is not correct!" "`hostname -f`" "${gfsdhost}"
    assertEquals "port is not correct!" "600" "${port}"
    assertNull   "listen address is not empty" "${listen_address}"
}

testGfsdConfsPrefixSpecifiedFmt2 () {
    read prefix gfsdhost port listen_address <<EOF
`parse_gfsd_confs "/usr/local:::"`
EOF
    assertEquals "failed to read" 0 $?
    assertEquals "prefix is not correct!" "/usr/local" "${prefix}"
    assertEquals "hostname is not correct!" "`hostname -f`" "${gfsdhost}"
    assertEquals "port is not correct!" "600" "${port}"
    assertNull   "listen address is not empty" "${listen_address}"
}

testGfsdConfsHostSpecified () {
    read prefix gfsdhost port listen_address <<EOF
`parse_gfsd_confs ":gfsd.gfarm.co.jp::"`
EOF
    assertEquals "failed to read" 0 $?
    assertEquals "prefix is not correct!" "/" "${prefix}"
    assertEquals "hostname is not correct!" "gfsd.gfarm.co.jp" "${gfsdhost}"
    assertEquals "port is not correct!" "600" "${port}"
    assertNull   "listen address is not empty" "${listen_address}"
}

testGfsdConfsListenAddressSpecified () {
    read prefix gfsdhost port listen_address <<EOF
`parse_gfsd_confs ":gfsd.gfarm.co.jp:gfsd.gfarm.co.jp:"`
EOF
    assertEquals "failed to read" 0 $?
    assertEquals "prefix is not correct!" "/" "${prefix}"
    assertEquals "hostname is not correct!" "gfsd.gfarm.co.jp" "${gfsdhost}"
    assertEquals "port is not correct!" "600" "${port}"
    assertEquals "listen addres is not correct!" "gfsd.gfarm.co.jp" "${listen_address}"

}

testGfsdConfsPortSpecifiedFmt1 () {
    read prefix gfsdhost port listen_address <<EOF
`parse_gfsd_confs "::10600"`
EOF
    assertEquals "failed to read" 0 $?
    assertEquals "prefix is not correct!" "/" "${prefix}"
    assertEquals "hostname is not correct!" "`hostname -f`" "${gfsdhost}"
    assertEquals "port is not correct!" "10600" "${port}"
    assertNull   "listen address is not empty" "${listen_address}"
}

testGfsdConfsPortSpecifiedFmt2 () {
    read prefix gfsdhost port listen_address <<EOF
`parse_gfsd_confs ":::10600"`
EOF
    assertEquals "failed to read" 0 $?
    assertEquals "prefix is not correct!" "/" "${prefix}"
    assertEquals "hostname is not correct!" "`hostname -f`" "${gfsdhost}"
    assertEquals "port is not correct!" "10600" "${port}"
    assertNull   "listen address is not empty" "${listen_address}"
}

testGfsdConfsSepareated () {
    (
        read prefix gfsdhost port listen_address
        assertEquals "failed to read" 0 $?
        assertEquals "prefix is not correct!" "/" "${prefix}"
        assertEquals "hostname is not correct!" "`hostname -f`" "${gfsdhost}"
        assertEquals "port is not correct!" "600" "${port}"
        assertNull   "listen_address is not empty" "${listen_address}"
        read prefix gfsdhost port listen_address
        assertEquals "failed to read" 0 $?
        assertEquals "prefix is not correct!" "/" "${prefix}"
        assertEquals "hostname is not correct!" "`hostname -f`" "${gfsdhost}"
        assertEquals "port is not correct!" "600" "${port}"
        assertNull   "listen_address is not empty" "${listen_address}"
    )<<EOF
`parse_gfsd_confs ":::%:::"`
EOF
}

testGfsdConfsEmptyIsInvalid () {
    parse_gfsd_confs ""
    assertEquals "invalid arg should exit with 1" 1 $?
}

testGfsdConfsTooFewColonIsInvalid () {
    parse_gfsd_confs ":"
    assertEquals "invalid arg should exit with 1" 1 $?
}

testGfsdConfsManyColonIsInvalid () {
    parse_gfsd_confs "::::"
    assertEquals "invalid arg should exit with 1" 1 $?
}

testGfmdConfsDefault () {
    :<<COMMENTOUT
    # expects /etc/gfmd.conf as:
    #==========================================
    # metadb_server_host dhcp-167-220.sra.co.jp
    # metadb_server_port 12345
    # postgresql_server_host dhcp-167-220.sra.co.jp
    # postgresql_server_port 5432
    # postgresql_dbname gfarm
    # postgresql_user gfarm
    # postgresql_password "02r86NcPiL0IYpt30z3CeVc55PcVpXrkb64=yg1vWhp"
    # admin_user gfadmin
    # auth enable sharedsecret *
    # 
    # sockopt keepalive
    # metadb_replication disable
    # # mkdir following directory when metadb_replication is set to enable.
    # metadb_journal_dir /opt/gfarmA/var/gfarm-metadata/journal
    #==========================================
    (
        read prefix host port
        assertEquals "failed to read" 0 $?
        assertEquals "prefix is not correct!" "/" "${prefix}"
        assertEquals "host is not correct!" "dhcp-167-220.sra.co.jp" "${host}"
        assertEquals "port is not correct!" "12345" "${port}"
    ) <<EOF
`read_gfmd_confs`
EOF
    assertEquals "failed to exec" 0 $?
COMMENTOUT
}

testGfmdConfsSeparatedDefaults () {
    :<<COMMENTOUT
    # expects /etc/gfmd.conf as:
    #==========================================
    # metadb_server_host dhcp-167-220.sra.co.jp
    # metadb_server_port 12345
    # postgresql_server_host dhcp-167-220.sra.co.jp
    # postgresql_server_port 5432
    # postgresql_dbname gfarm
    # postgresql_user gfarm
    # postgresql_password "02r86NcPiL0IYpt30z3CeVc55PcVpXrkb64=yg1vWhp"
    # admin_user gfadmin
    # auth enable sharedsecret *
    #
    # sockopt keepalive
    # metadb_replication disable
    # # mkdir following directory when metadb_replication is set to enable.
    # metadb_journal_dir /opt/gfarmA/var/gfarm-metadata/journal
    #==========================================
    (
        read prefix host port
        assertEquals "failed to read" 0 $?
        assertEquals "prefix is not correct!" "/" "${prefix}"
        assertEquals "host is not correct!" "dhcp-167-220.sra.co.jp" "${host}"
        assertEquals "port is not correct!" "12345" "${port}"
        read prefix host port
        assertEquals "failed to read" 0 $?
        assertEquals "prefix is not correct!" "/" "${prefix}"
        assertEquals "host is not correct!" "dhcp-167-220.sra.co.jp" "${host}"
        assertEquals "port is not correct!" "12345" "${port}"
    ) <<EOF
`read_gfmd_confs "%"`
EOF
    assertEquals "failed to exec" 0 $?
COMMENTOUT
}

testGfarmPgsqlConfsDefault () {
    # expects /etc/gfmd.conf as:
    #==========================================
    # metadb_server_host dhcp-167-220.sra.co.jp
    # metadb_server_port 12345
    # postgresql_server_host dhcp-167-220.sra.co.jp
    # postgresql_server_port 5432
    # postgresql_dbname gfarm
    # postgresql_user gfarm
    # postgresql_password "02r86NcPiL0IYpt30z3CeVc55PcVpXrkb64=yg1vWhp"
    # admin_user gfadmin
    # auth enable sharedsecret *
    #
    # sockopt keepalive
    # metadb_replication disable
    # # mkdir following directory when metadb_replication is set to enable.
    # metadb_journal_dir /opt/gfarmA/var/gfarm-metadata/journal
    #==========================================
    (
        read prefix port db user pass
        assertEquals "port is not correct!" "5432" "${port}"
        assertEquals "db is not correct!" "gfarm" "${db}"
        assertEquals "user is not correct!" "gfarm" "${user}"
        assertEquals "pass is not correct!" "02r86NcPiL0IYpt30z3CeVc55PcVpXrkb64=yg1vWhp" "${pass}"
    ) <<EOF
`read_gfarm_pgsql_confs ""`
EOF
    assertEquals "failed to exec" 0 $?
}

testAverage () {
    result=`echo 5 | average_or_error`
    assertEquals "failed to exec" 0 $?
    assertEquals "expect result" 5 $result
    result=`(echo 1; echo 3) | average_or_error`
    assertEquals "failed to exec" 0 $?
    assertEquals "expect result" 2 $result
}

testAverageWithNegativeValue () {
    result=`echo -1 | average_or_error`
    assertEquals "failed to exec" 0 $?
    assertEquals "expect error" -1 $result
    result=`(echo 1; echo 3; echo -1; echo 4) | average_or_error`
    assertEquals "failed to exec" 0 $?
    assertEquals "expect error" -1 $result
}

testAverageNoInput () {
    result=`echo | average_or_error`
    assertEquals "failed to expect" 0 $?
    assertEquals "expect error" -1 $result
}

testPsqlWrapper () {
    # and database 'gfarm' and table dog as:
    #  id | name
    #   1 | momo
    #   2 | pochi
    result=`psql_wrapper \
            localhost \
            5432 \
            gfarm \
            gfarm \
            "02r86NcPiL0IYpt30z3CeVc55PcVpXrkb64=yg1vWhp" \
            "SELECT COUNT(*) FROM dog"`
    assertEquals "failed to exec" 0 $?
    assertEquals "expect result" "2" ${result}
}

testGfarmPgsqlExec () {
    # expect gmfd.conf, and database 'gfarm' and table dog as:
    #  id | name
    #   1 | momo
    #   2 | pochi
    result=`gfarm_pgsql_exec "" 'SELECT COUNT(*) FROM dog'`
    # assertEquals "failed to exec" 0 $?
    assertEquals "expect result" "2" ${result}
}

testGfarmPgsqlExecAndGrouping () {
    # expect gmfd.conf, and database 'gfarm' and table dog as:
    #  id | name
    #   1 | momo
    #   2 | pochi
    result=`gfarm_pgsql_exec "/%/" 'SELECT COUNT(*) FROM dog' | average_or_error`
    # assertEquals "failed to exec" 0 $?
    assertEquals "expect result" "2" ${result}
}


. shunit2
