hadoop fs -rm -r ~/test/2gterasort/output
time hadoop jar /home/hadoop/hadoop-2.5.2/share/hadoop/mapreduce/hadoop-mapreduce-examples-2.5.2.jar terasort ~/test/2gterasort/input ~/test/2gterasort/output
