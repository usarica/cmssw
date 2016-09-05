#!/bin/bash

track_type=$1
let nfiles_per_job=1 # Number of files per job. Comma-separated names indicate files to process within the job; newlines separate the different jobs.
if [[ "$2" != "" ]]; then
   let nfiles_per_job=$2
fi

for i in $(ls *$track_type*)
do
   j="ALCARECOTkAl"$track_type".dat_"${i%.*};
   echo $j
   . rewrite.sh $i $j $nfiles_per_job
done

