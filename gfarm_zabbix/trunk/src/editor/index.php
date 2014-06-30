<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8">
<title>'gfarm2.conf' editor</title>
<style type="text/css" media="all">
textarea {
    font-family: monospace;
    border-style: solid;
    border-color: black;
    border-width: 1px;
    padding: 8px;
    background-color: #c0c0f0;
}
td.caption {
    font-weight: bold;
}
td.button {
    text-align: right;
}
td.footer {
    text-align: right;
}
</style>
</head>

<body>
<h1>'gfarm2.conf' editor</h1>

<?php include('./common.php'); ?>

<table>
<thead>
<tr>
<td class="caption">Current 'gfarm2.conf' file:
<td class="button"><a href="download.php">[download]</a>
<a href="edit.php">[edit]</a>
</tr>
</thead>
<tbody>
<tr>
<td class="view" colspan="2">
<textarea name="conf" cols="80" rows="15" readonly>
<?php
$lines = read_gfarm2_conf_file();
if (count($lines) > 0 && !ereg('^# Error:', $lines[0])) {
    if ($stat = stat($gfarm2_conf_file)) {
        printf("# This file was edited on: %s\n",
            strftime("%Y-%m-%d %H:%M:%S", $stat[9]));
    }
}
foreach ($lines as $line) {
    printf("%s\n", htmlspecialchars($line));
}

print("\n");

$lines = read_gfmdlist_file();
if (count($lines) > 0 && !ereg('^# Error:', $lines[0])) {
    if ($stat = stat($gfmdlist_file)) {
        printf("# The 'metadb_server_list' line was updated on: %s\n",
            strftime("%Y-%m-%d %H:%M:%S", $stat[9]));
    }
}
foreach ($lines as $line) {
    printf("%s\n", htmlspecialchars($line));
}
?>
</textarea>
</tr>
</tbody>
<tr>
<td>
<td class="footer">
</table>

</body>
</html>
