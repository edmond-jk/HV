#!/usr/bin/env python

import re, csv, sys

#too lazy to use optparse
f = open(sys.argv[1],"r")

#vdbench output contains random blank lines and repeating column headers.  If the line doesn't start with a number we don't need it
linematch=re.compile(r'^\d')
linelist=list()

for line in f:
		matchobj=linematch.search(line)
		if matchobj: 
				linelist.append(line.split()) 
				#last line in vdbench output is always an average... we don't need those figures as we're going to suck output into pandas anyways..
				csvfile = open('vdbench_output.csv','wb')
				writer = csv.writer(csvfile,dialect='excel')
				#write out dataframe friendly header
				writer.writerow(['time','interval','io_rate','MB_sec','bytes_io','read_pct','resp_time','resp_max','resp_stddev','cpu_sysusr','cpu_sys'])
				for i in linelist:
						writer.writerow(i)
