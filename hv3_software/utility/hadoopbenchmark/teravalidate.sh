hadoop fs -rm -r ~/test/2gterasort/report
time hadoop jar /home/hadoop/hadoop-2.5.2/share/hadoop/mapreduce/hadoop-mapreduce-examples-2.5.2.jar teravalidate ~/test/2gterasort/output ~/test/2gterasort/report
hadoop fs -ls ~/test/2gterasort/report
