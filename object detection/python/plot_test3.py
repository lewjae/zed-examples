import numpy as np
import matplotlib.pyplot as plt 
import time

fig = plt.figure()
axis = plt.axes(xlim=(0,6),ylim=(0,6))
xdata, ydata = [1,2,3,4,5], [1,2,3,4,5]

for i in range(len(xdata)):
	plt.scatter(xdata[i],ydata[i])
	print("JAE",i)
	plt.draw()
	plt.pause(1)
	#plt.show()