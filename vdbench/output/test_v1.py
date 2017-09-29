import time, os
import csv
from datetime import datetime
import matplotlib.pyplot as plt
from matplotlib import animation 

style.use('fivethirtyeight')

fig = pit.figure()
ax1 = fig.add_subplot(1,1,1)

def animate(i): 
		reader = file("flatfile.html","r") 
		
		while True:
				where = reader.tell()
				line = reader.readline() 
		
		if not line:
				time.sleep(1)
				reader.seek(where)
		else: 
				this_line = 1 
				
				if len(line.strip()) <= 0 or line.startswith("*"):
						this_line = 0 
				
				if "tod" in line or "avg" in line or "Vdbench" in line:
						this_line = 0 
								
				if (this_line != 0):
						process_line = line.split( )
						temp = process_line[0].split(':')
						exec_time = int(temp[0]) *3600 + int(temp[1])*60 + float(temp[2])
						throughput = float(process_line[6].strip())
						x.append(exec_time)
						y.append(throughput)
						plt.show()
						plt.pause(0.0001)
						
					




						
					#	plt.plot(exec_time, throughput)
					#	plt.title("1")
					#	plt.draw()

#plt.show(block=True)
					#	print exec_time
					#	print throughput	
					#	print ("x:{0}, y:{1}".format(exec_time, throughput))
					#	
		
#for row in csv_reader:
#		print row

#workouts = parse_workouts(csv_reader)

#for w in workouts:
#		print w 

#times = extract_times(workouts)
#throughputs = extract_throughputs(workouts)

#plt.plot(times, throughputs)
#plt.show()


