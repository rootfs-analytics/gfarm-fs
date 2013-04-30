<?php
$filepath = "files/gfarm2.conf";
header('Content-Disposition: attachment; filename="'.basename($filepath).'"');
//header('Content-Type: application/octet-stream');
header('Content-Type: text/plain');
//header('Content-Transfer-Encoding: binary');
header('Content-Transfer-Encoding: 8bit');
header('Content-Length: '.filesize($filepath));
readfile($filepath);
?>
