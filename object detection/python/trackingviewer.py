import math as m

class TrackingViewer():
	def __init__(self):
		# Objetct range
		self.x_min = -6250.0
		self.x_max = - self.x_min
		self.z_min = -12500.0

		# Window size
		self.window_width = 800
		self.window_height = 800

		# Visualization configuration
		self.end_of_track_color = (255,40,40)
		self.camera_offset = 50
		self.x_step = (self.x_max - self.x_min) / self.window_width
		self.z_step = abs(self.z_min) / self.window_height

		# History management
		self.min_length_to_draw = 3

		# Configure thru FPS information
		self.fps = 30
		self.configureFromFPS()

		# Camera Settings
		fov = -1.0

		background_color = (248,248,248)
		has_background_ready = False

		# Smooth
		do_smooth = False



	def configureFromFPS(self):
		self.frame_time_step = m.ceil(1000000000.0 / self.fps)

		# Show last 1.5 seconds
		history_size = int(1.5 * self.fps)

		# Threshold to delete track
		max_missing_points = max(self.fps/6.0, 4)

		# Smoothing window: 80ms
		smoothing_window_size = m.ceil(0.08 * self.fps)

	def setZMin(self,z_):
		self.z_min = z_
		self.x_min = z_ / 2.0
		self.x_max = -self.x_min
		self.x_step = (self.x_max - self.x_min) / self.window_width
		self.z_step = abs(self.z_min) / (self.window_height - self.camera_offset)

	def setFPS(self, fps_, configure_all):
		self.fps = fps_
		self.frame_time_step = m.ceil(1000000000.0 / self.fps)
		if configure_all:
			self.configureFromFPS()

	def setCameraCalibration(self, calib):
		self.camera_calibration = calib	
