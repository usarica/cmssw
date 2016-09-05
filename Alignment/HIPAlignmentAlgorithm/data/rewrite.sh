#!/bin/bash

in_file=$1 # Name of input file
out_file=$2 # Name of output file. Example: ALCARECOTkAlMinBias.dat_2016B_Prompt_v1, where the format is [reco track].dat_[appended name]
nfiles_per_job=1 # Number of files per job. Comma-separated names indicate files to process within the job; newlines separate the different jobs.
if [[ "$3" != "" ]]; then
   let nfiles_per_job=$3
fi


let i=0
while read line
do
   name=$line
   let i=i+1
   if [ $(($i%$nfiles_per_job)) -eq 0 ]; then
      echo "'$name'" >> $out_file
   else
      echo "'$name'" | tr "\n" "," >> $out_file
   fi
   #if [ $i -eq 51 ]
   #   then break
   #fi
done < $in_file
