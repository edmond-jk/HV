import csv
from datetime import datetime
import matplotlib.pyplot as plt
import matplotlib.animation as animation


fig = plt.figure()
ax1 = fig.add_subplot(1,1,1)

ax1.set_xlabel('seconds')
ax1.set_ylabel('MBPS')

ax1.set_title('Performance')

#-----------------------------------------------------------------
def keep_line(line):
		this_line = 1
		if len(line.strip()) <= 0 or line.startswith("*"):
				this_line = 0
		if "tod" in line or "avg" in line or "Vdbench" in line:
				this_line = 0
		if "SKT" in line or "Date" in line:
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
def parse_vdbench(rows):
		workouts = []
		for row in rows:
				temp = row[0].split(':')
				time = int(temp[0]) *3600 + int(temp[1])*60 + float(temp[2])

				throughput = float(row[6])

				workouts.append([time, throughput])

		return workouts

def parse_intelmonitor(rows):
		workouts = []
		for row in rows:
				temp = row[1].split(':')
				time = int(temp[0]) *3600 + int(temp[1])*60 + float(temp[2]) 
				
				throughput = float(row[10]) 
				workouts.append([time, throughput]) 
	
		return workouts

def parse_hvperf(rows):
		workouts = []
		for row in rows:
				time = int(row[0])
				throughput = float(row[1]) 
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


def animate_graph(i): 
    reader_hv = file("/sys/kernel/debug/hvperf_log","r")
    reader_vd = file("/home/lab/Tools/vdbench/output/flatfile.html","r")
    reader_mm = file("/home/lab/work/mem_traffic.txt","r")
    lines_hv = filter_lines(reader_hv) 
    lines_vd = filter_lines(reader_vd) 
    lines_mm = filter_lines(reader_mm) 
    
    times_hv = []
    throughputs_hv = [] 

    times_vd = []
    throughputs_vd = [] 
    
    times_mm = []
    throughputs_mm = [] 
    
    csv_reader_hv = csv.reader(lines_hv, delimiter=',',skipinitialspace=True) 
    workouts_hv = parse_hvperf(csv_reader_hv) 
    times_hv = extract_times(workouts_hv)
    throughputs_hv = extract_throughputs(workouts_hv) 
    
    csv_reader_vd = csv.reader(lines_vd, delimiter=' ',skipinitialspace=True) 
    workouts_vd = parse_vdbench(csv_reader_vd) 
    times_vd = extract_times(workouts_vd)
    throughputs_vd = extract_throughputs(workouts_vd) 
    
    csv_reader_mm = csv.reader(lines_mm, delimiter=';',skipinitialspace=True) 
    workouts_mm = parse_intelmonitor(csv_reader_mm) 
    times_mm = extract_times(workouts_mm)
    throughputs_mm = extract_throughputs(workouts_mm) 
    
    ax1.clear()
    ax1.plot(times_hv, throughputs_hv, 'r', linewidth=3, label='HVDIMM PERFORMANCE')
    ax1.plot(times_vd, throughputs_vd, 'g', linewidth=3, label='VDBENCH') 
    ax1.plot(times_mm, throughputs_mm, 'b', linewidth=3, label='Intel Monitor') 
    ax1.set_xlabel('Time')
    ax1.set_ylabel('MBPS') 
    ax1.set_title('IO Performance')

   

ani = animation.FuncAnimation(fig, animate_graph, interval=1000)
plt.show()




