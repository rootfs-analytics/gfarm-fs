<?php
# 'gfarm2.conf' file.
$gfarm2_conf_file = 'skeleton/gfarm2.conf';

# file which stores result of 'gfmdhost -l'.
$gfmdlist_file = 'gfmdlist/metadataserver_list.log';

#
# Read '$gfarm2_conf_file' file.
#
function read_gfarm2_conf_file() {
    global $gfarm2_conf_file;

    $lines = array();
    $handle = fopen($gfarm2_conf_file, 'rb');
    if ($handle == FALSE) {
        $line = sprintf("# Error: failed to open the file '%s'",
            $gfarm2_conf_file);
        array_push($lines, $line);
    } else {
        while (($line = fgets($handle)) != FALSE) {
            $line = rtrim($line);
            if (!ereg('^[ \t\f\v\'\"]*metadb_server_list([^A-Za-z0-9_]|$)',
                $line)) {
                array_push($lines, $line);
            }
        }
        fclose($handle);
    }

    while (count($lines) > 0 && end($lines) == "") {
        array_pop($lines);
    }

    return $lines;
}

#
# Read '$gfmdlist_file' file.
#
function read_gfmdlist_file() {
    global $gfmdlist_file;

    $lines = array();
    $handle = fopen($gfmdlist_file, 'rb');
    if ($handle == FALSE) {
        $line = sprintf("# Error: failed to open the file '%s'",
            $gfmdlist_file);
        array_push($lines, $line);
    } else {
        while (($line = fgets($handle)) != FALSE) {
            array_push($lines, rtrim($line));
        }
        fclose($handle);
    }

    return $lines;
}

#
# Write '$gfarm2_conf_file' file.
#
function write_gfarm2_conf($conf_data) {
    global $gfarm2_conf_file;

    $tmp_file = $gfarm2_conf_file . '.tmp';
    $handle = fopen($tmp_file, 'wb');
    if (!$handle) {
        return FALSE;
    }
    fwrite($handle, $conf_data);
    fclose($handle);
    if (!rename($tmp_file, $gfarm2_conf_file)) {
        return FALSE;
    }

    return TRUE;
}
?>
