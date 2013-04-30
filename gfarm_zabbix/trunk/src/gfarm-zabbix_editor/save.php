<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8">
<title>'gfarm2.conf' editor</title>
<style type="text/css" media="all">
p.center {
    text-align: center;
}
</style>
</head>

<body>
<h1>'gfarm2.conf' editor</h1>

<form>
<p class="center">
<?php
include('./common.php');

$result = TRUE;
if (array_key_exists('conf', $_POST)) {
    if (!write_gfarm2_conf($_POST['conf'])) {
        print("Failed to update 'gfarm2.conf'.");
        $result = FALSE;
    }
} else {
    print("No data.\n");
    $result = FALSE;
}

print("<br><br>\n");

if ($result) {
    header("HTTP/1.1 303 See Other");
    header("Location: index.php");
    exit();
} else {
    print('<input type="button" onClick="history.back();" value="Back">');
    print("\n");
}
?>
</p>
</form>

</body>
</html>
