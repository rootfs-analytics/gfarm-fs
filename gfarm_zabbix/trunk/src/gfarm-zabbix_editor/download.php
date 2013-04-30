<?php
include('./common.php');
header('Content-Disposition: attachment; filename="gfarm2.conf"');
header('Content-Type: text/plain');
header('Content-Transfer-Encoding: 8bit');

$lines = read_gfarm2_conf_file();
foreach ($lines as $line) {
    printf("%s\n", $line);
}

print("\n");

$lines = read_gfmdlist_file();
foreach ($lines as $line) {
    printf("%s\n", $line);
}
?>
