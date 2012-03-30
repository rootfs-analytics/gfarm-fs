<?php
/*
** regist.php
** 
** A PHP script for checking Gfarm2 client configuration file
** uploaded by upload.html.
**/
?>
<?php
function get_additional_config() { 
  $cmd = "zbx_generate_mdslist.sh";
  $result = shell_exec("$cmd");
  return $result;
}

function edit_config_file($filepath) { 
  $result = get_additional_config();

  $fp = fopen($filepath, "a");
  $line = "\n" . $result;
  fwrite($fp, $line);
  fclose($fp);

  return 0;
}

function check_config_file($filepath) {
  $cmd = "zbx_check_gfarm2_conf.sh";
  $result = shell_exec("$cmd $filepath");
  return $result;
}

function display_text_file($filepath, $name) { 
  echo "<textarea name=\"$name\" cols=100 rows=10 readonly>";
  readfile($filepath);
  echo "</textarea>";
}

function display_download_button() { 
  echo "<form name=\"filedownload\" method=\"POST\" enctype=\"multipart/form-data\" action=\"download.php\">";
  echo "<input type=\"submit\" name=\"download\" value=\"Download\" />";
  echo "</form>";
}

function display_back_button() { 
  echo "<form name=\"back\" action=\"upload.html\">";
  echo "<input type=\"submit\" name=\"back\" value=\"Back\" />";
  echo "</form>";
}

function download_config_file($filepath) {
  header('Content-Disposition: attachment; filename="'.basename($filepath).'"');
  header('Content-Type: application/octet-stream');
  header('Content-Transfer-Encoding: binary');
  header('Content-Length: '.filesize($filepath));
  readfile($filepath);
}

?>

<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8" />
<title>gfarm2.conf sample generator</title>
</head>
<body>
<p><?php

  $tmp_name = $_FILES["upfile"]["tmp_name"];
  $name = $_FILES["upfile"]["name"];
  $path = "files/" . "gfarm2.conf";

  if (! is_uploaded_file($tmp_name)) {
    echo "ファイルが選択されていません。";
    display_back_button();
    return 1;
  }

  if (! move_uploaded_file($tmp_name, $path)) {
    echo "ファイルをアップロードできません。";
    display_back_button();
    return 1;
  }

  /* set permission */
  chmod($path, 0644);

  /* generate configuration file */
  edit_config_file($path);

  /* print the generated file */
  echo "以下の Gfarm2 クライアント設定ファイルを生成しました。<br />";
  display_text_file($path, "gfarm2.conf");
  echo "<br />";

  /* check configuration file */
  $chk_res = check_config_file($path);
  if (strncmp($chk_res, "success", 7) == 0) {
    /* download button */
    display_download_button();
  } else {
    if (strncmp($chk_res, "timeout", 7) == 0) {
      echo "<br />";
      echo "Gfarm2 クライアント設定ファイル確認環境に問題があります($chk_res)。<br />";
    } else {
      echo "<br />";
      echo "アップロードされた Gfarm2 クライアント設定ファイルが不正です($chk_res)。<br />";
    }

    $errfile="/tmp/gfls_error.txt";
    display_text_file($errfile, "error_msg");
  }

  /* back button */
  display_back_button();
?></p>
</body>
</html>
