#!/bin/bash
####################################################################
# ファイル名  :m46e-pr_ex-co.sh                                    #
# 機能概要    :Sa46t-PR外部連携                                    #
# 修正履歴    : 2014.01.22 M.Iwatsubo 新規作成                     #
#               2016.04.15 H.Koganemaru 名称変更に伴う修正         #
#                                                                  #
# ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2014-2016      #
####################################################################
usege ()
{
	echo "Usage: m46e-pr_ex-co.sh get PLANE_ID PLANE_NAME URL USER PASS"
	echo "       m46e-pr_ex-co.sh exe PLANE_ID PLANE_NAME FILE_NAME"
	echo "       m46e-pr_ex-co.sh all PLANE_ID PLANE_NAME URL USER PASS"
	echo "// command explannations //"
	echo "get  :Get M46E-PR information from the server."
	echo "exe  :Add the M46E-PR Entry to M46E-PR Table."
	echo "all  :Get M46E-PR information from the server and Add the M46E-PR Entry to M46E-PR Table."
}
if test $# -eq 0
then 
	usege
	exit 1
fi

if test $1 != all -a $1 != get -a $1 != exe
then
	usege
	exit 1
fi	

if test $# -ne 6 -a $1 = all
then
	usege 
	exit 1
fi

if test $# -ne 6 -a $1 = get
then
	usege
	exit 1
fi

if test $# -ne 4 -a $1 = exe
then
	usege
	exit 1
fi

	#allまたは取得処理
	if test $1 = all -o $1 = get
	then
	#変数設定
		#id     ⇒　　plane_id
		#name   ⇒　　plane_name
		#url    ⇒　　取得するファイルのURL
		#user   ⇒　　取得する際のUSER_NAME
		#pass   ⇒　　取得する際のPASSWORD
		if test $1 = all
		then
			id=$2
			name=$3
			url=$4
			user=$5
			pass=$6
		elif test $1 = get
		then
			id=$2
			name=$3
			url=$4
			user=$5
			pass=$6
		fi

		time=`date +"%m%d_%H-%M-%S"`

		#ファイルをサーバから取得
		wget -O $name-$time.txt --http-user="$user" --http-passwd="$pass" $url
		
		if test $? = 0
		then
			#plane_idのみを抽出
			
			plane_id=$(sed -ne "s/plane_id[[:blank:]]\{0,\}=[[:blank:]]\{0,\}//p" $name-$time.txt)


			#コマンド引数と取得したPR情報のplane_idが
			#一致しているか確認
			if test $plane_id = $id
			then
				#デモ用コメント（デモ後コメント削除)
				echo "File transfer completion "
			else
			#取得ファイル削除
				rm $name-$time.txt
			echo "ID is different"
			fi
		else
			rm $name-$time.txt
			#デモ用コメント（デモ後コメント削除）
			echo "Faild to download M46E-PR information."
			exit 1
		fi
	fi	
		
	#allまたは実行処理	
	if test $1 = all -o $1 = exe
	then
		if test $1 = exe
		then
			id=$2
			name=$3
			file_name=$4

		else 
			file_name=$name-$time.txt
		fi

		#ここでファイルチェックを行う処理を追加する
		if test -e $file_name
		then
			plane_id=$(sed -ne "s/plane_id[[:blank:]]\{0,\}=[[:blank:]]\{0,\}//p" $file_name)

				if test $plane_id = $id
				then

				#変換用ファイルを作成
					touch cnv.txt
					#PR情報のみ記載された箇所のみをコマンド変換用ファイルに置換

					sed -ne "/^[0-9]\{1,3\}\.[0-9]\{1,3\}\.[0-9]\{1,3\}\.[0-9]\{1,3\}\/[0-9]\{1,2\}[[:blank:]]\+/p" $file_name  > cnv.txt

					sed -i -e "s/^/add pr /" cnv.txt

					sed -i -e "s/\$/ enable/" cnv.txt
					#試験後削除するコメント
					echo "It succeeded in change of a file."

					#コマンド実行処理
					path=$(which m46ectl)

					#m46ectlコマンドが存在していればコマンド実行
					#存在しない場合はエラーである
					if test $? = 0
					then

						#外部連携では追加処理のみを行う。
						#元の情報は削除しないため、PR情報全削除処理はコメントアウト。
						#sudo $path -n $name delall pr

						#PR情報追加
						sudo $path -n $name load pr cnv.txt

						#コマンド実行後変換ファイルを削除
						rm cnv.txt

						echo "It succeeded in execution. "

					else
						echo "The command of m46ectl does not exist."

						rm cnv.txt
					fi
				else
					echo "ID is different "	
				fi
		else
			echo "There is no file. "
		fi
	fi
