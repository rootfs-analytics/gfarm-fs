#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# NAME
#     format.py - Template_Gfarm_exported_all.xml を生成する
#
# SYNOPSIS
#     format.py
#
# DESCRIPTION
#     format.py は、gfarm-zabbix 用の監視テンプレート XML ファイル
#     Template_Gfarm_exported_all.xml の生成を行う Python スクリプトです。
#     動作確認は Python 2.7 で行っています。
#
#     もともと、gfarm-zabbix 用の監視テンプレートは単一の XML ファイルの
#     形で提供していたわけではなく、たとえば gfmd の監視であれば
#     Template_Gfarm_common.xml と Template_Gfarm_redundant_gfmd.xml を
#     Zabbix にインポートするようになっていました。しかし、この方式では
#     いくつものテンプレートをインポートする必要があり、手順が煩雑でした。
#
#     そこで、単一の XML ファイルを用意することにしました。Zabbix には
#     インポートしたファイルを一括エクスポートして単一の XML ファイルと
#     して出力する機能があるので、常識的に考えればそれを使えば済む筈です。
#     しかし残念なことに、Zabbix から一括エクスポートした XML ファイルを
#     再びインポートすると、エラーになります。原因は、Zabbix が監視項目間
#     の依存関係を考慮せずにエクスポートを行うため、監視項目の定義順序の
#     が正しくないからです。
#
#     本スクリプトは、Zabbix から一括エクスポートした XML ファイルの定義
#     順序を直し、再度インポートできる形にします。
#
#     Zabbix から一括エクスポートした XML ファイルをカレントディレクトリ
#     に Template_Gfarm_exported_all.xml というファイル名で配置した状態で
#     本スクリプトを実行して下さい。Template_Gfarm_exported_all.xml は
#     定義順序が修正された形のものに上書きされます。
#

from xml.etree.ElementTree import ElementTree

if __name__ == '__main__':
    xml = "Template_Gfarm_exported_all.xml"

    tree = ElementTree()
    tree.parse(xml)
    hosts = tree.find('.//hosts')

    host_list = tree.findall('.//host')
    for host in host_list:
        hosts.remove(host)

    for host in host_list:
        if host.attrib['name'].endswith('_nodep'):
            hosts.append(host)

    for host in host_list:
        if not host.attrib['name'].endswith('_nodep'):
            hosts.append(host)

    tree.write(xml, encoding="UTF-8", xml_declaration=True, method="xml")
