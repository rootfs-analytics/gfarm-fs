<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8">
<title>'gfarm2.conf' editor</title>
<style type="text/css" media="all">
textarea {
    font-family: monospace;
}
td.caption {
    font-weight: bold;
}
td.button {
    text-align: right;
}
</style>
</head>

<body>
<h1>'gfarm2.conf' editor</h1>

<?php include('./common.php'); ?>

<form name="save" method="POST" enctype="multipart/form-data" 
    action="save.php">
<table>
<thead>
<tr>
<td class="caption">Edit 'gfarm2.conf' file:
<td class="button"><a href="#" onClick="f=history.back()">[cancel]</a>
<a href="#" onClick="f=document.save.submit()">[save]</a>

</tr>
</thead>
<tbody>
<tr>
<td colspan="2">
<textarea name="conf" cols="80" rows="15">
<?php
$lines = read_gfarm2_conf_file();
foreach ($lines as $line) {
    printf("%s\n", htmlspecialchars($line));
}
?>
</textarea>
</form>
</tr>
</tbody>
</table>
</form>

<p>
Note that 'metadb_server_list' lines are discarded.<br>
'metadb_server_list' line will be added dynamically by the utility.
</p>

</body>
</html>
