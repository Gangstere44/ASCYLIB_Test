#!/bin/bash

prog=$1;
shift;

n=$1;
shift;

b=$1;
shift;

min_inter=$1;
shift;

max_inter=$1;
shift;

min_avg=1000000;
min_inter=0;

for (( t=$min_inter; t<=$max_inter; t+=100))
do

	tmp=data.tmp;
	printf "" > $tmp;

	for i in {1..5}
	do

		./$prog -a $b -t $t -n $n | grep "Mops" >> $tmp;
	done

	count=0;
	total=0;

	for i in $(awk '{print $2; }' $tmp)
	do 
		total=$(echo $total+$i | bc)
		((count++))
	done

	avg=$(echo "scale=3; $total / $count" | bc);
	
	#echo "Mops with interval $t and n $n : $avg"

	if (( $(bc <<< "$min_avg > $avg") ))
	then
		min_avg=$avg;
		min_inter=$t;
	fi
done

echo "Best Mops is with interval $min_inter => $min_avg"

