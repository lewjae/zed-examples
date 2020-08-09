import numpy as np
import matplotlib.pyplot as plt 
import matplotlib.animation as animation

fig = plt.figure()
axis = plt.axes(xlim=(-50,50),ylim=(-50,50))
line, = axis.plot([], [], marker = 'o', lw = 0)
xdata, ydata = [], []

def init(): # give a clean slate to start
	line.set_data([],[])
	return line,

def animate(i): #update the y values every 1000ms
	t = 0.1 * i
	x = t * np.sin(t)
	y = t * np.cos(t)
	xdata.append(x)
	ydata.append(y)
	line.set_data(xdata,ydata)
	return line,

ani = animation.FuncAnimation(fig,animate,  init_func=init, frames=400, interval=20, blit=True, save_count = 10)

plt.show()