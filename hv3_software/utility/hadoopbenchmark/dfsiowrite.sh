hadoop fs -rm ~/tmp/TestDFSIOwrite.txt
#time hadoop jar /home/hadoop/hadoop-2.5.2/share/hadoop/mapreduce/hadoop-mapreduce-client-jobclient-2.5.2-tests.jar TestDFSIO -write -nrFiles 10 -fileSize 100
time hadoop jar /home/hadoop/hadoop-2.5.2/share/hadoop/mapreduce/hadoop-mapreduce-client-jobclient-2.5.2-tests.jar TestDFSIO -write -nrFiles 10 -fileSize 1000
#time hadoop jar /home/hadoop/hadoop-2.5.2/share/hadoop/mapreduce/hadoop-mapreduce-client-jobclient-2.5.2-tests.jar TestDFSIO -write -nrFiles 64 -fileSize 2GB -resFile ~/tmp/TestDFSIOwrite.txt
