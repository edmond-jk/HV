import csv
from datetime import datetime
import matplotlib.pyplot as plt
import matplotlib.animation as animation


fig = plt.figure()
ax1 = fig.add_subplot(1,1,1)

#-----------------------------------------------------------------
def keep_line(line):
		this_line = 1
		if len(line.strip()) <= 0 or line.startswith("*"):
				this_line = 0
		if "tod" in line or "avg" in line or "Vdbench" in line:
				this_line = 0
			
		return this_line

#-----------------------------------------------------------------
def filter_lines(reader):
		lines = []
		
		for line in reader:
				if keep_line(line):
						lines.append(line)
						
		return lines
#----------------------------------------------------------------
def parse_workouts(rows):
		workouts = []
		for row in rows:
				temp = row[0].split(':')
				time = int(temp[0]) *3600 + int(temp[1])*60 + float(temp[2])

				throughput = float(row[5])

				workouts.append([time, throughput])

		return workouts
#----------------------------------------------------------------
def extract_times(workouts):
		times = []

		for w in workouts:
				times.append(w[0])

		return times
#----------------------------------------------------------------
def extract_throughputs(workouts):
		throughputs = []

		for w in workouts:
				throughputs.append(w[1])

		return throughputs

def animate(i):
		reader = file("flatfile.html","r")
		lines = filter_lines(reader)

		times = []
		throughputs = []

		csv_reader = csv.reader(lines, delimiter=' ',skipinitialspace=True) 
		workouts = parse_workouts(csv_reader) 
	
		times = extract_times(workouts)
		throughputs = extract_throughputs(workouts)

		ax1.clear()
		ax1.plot(times, throughputs)

ani = animation.FuncAnimation(fig, animate, interval=1000)
plt.show()





