#!/bin/bash

add_name=""
if [[ "$1" != "" ]];then
   add_name="_"$1
fi

let set_merge=0
if [[ "$2" != "" ]];then
   let set_merge=$2
fi

for name in "Cosmic" "MinBias" "ZMuMu"
do

   lst_file=""
   if [ $set_merge -eq 0 ];then
      lst_file="../scripts/"$name$add_name".lst"
   else
      lst_file="../scripts/AllTracks"$add_name".lst"
   fi

   if [[ "$name" == "Cosmic" ]];then
      track_type="COSMICS"
   else
      track_type="MBVertex"
   fi

   for i in $(ls *$name*)
   do
      if [[ "$i" == "ALCARECOTkAl"* ]];then
         echo "data/"$i","$track_type >> $lst_file
      fi
   done

done

