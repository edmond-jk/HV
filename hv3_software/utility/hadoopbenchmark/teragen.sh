hadoop fs -rm -r ~/test/2gterasort/input
time hadoop jar /home/hadoop/hadoop-2.5.2/share/hadoop/mapreduce/hadoop-mapreduce-examples-2.5.2.jar teragen 10000000 ~/test/2gterasort/input
