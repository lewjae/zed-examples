########################################################################
#
# Copyright (c) 2020, STEREOLABS.
#
# All rights reserved.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
########################################################################

import sys
import pyzed.sl as sl

sys.path.remove('/opt/ros/kinetic/lib/python2.7/dist-packages')

import cv2
import numpy as np
import argparse
from trackingviewer import TrackingViewer 

id_colors = [(59, 232, 176),
             (25,175,208),
             (105,102,205),
             (255,185,0),
             (252,99,107)]
red_color = (0,0,255)

def get_color_id_gr(idx):
    color_idx = idx % 5
    arr = id_colors[color_idx]
    return arr

def project3Dto2D(pt3D):
    #[LEFT_CAM_HD]
    fx=527.62
    fy=527.045
    cx=642.77
    cy=365.4935
    k1=-0.0441568
    k2=0.0126964
    k3=-0.00563514
    p1=0.00051255
    p2=-3.78875e-06

    x_ = pt3D[0]/pt3D[2]
    y_ = pt3D[1]/pt3D[2]
    r2 = x_*x_ + y_*y_
    k = 1 + k1*r2 + k3*r2*r2 + k3*r2*r2*r2
    x__ = x_*k + 2*p1*x_*y_ + p2*(r2+2*x_*x_)
    y__ = y_*k + 2*p2*x_*y_ + p1*(r2+2*y_*y_)
    u = int(fx*x_ + cx) 
    v = int(fy*y_ + cy) 
    return (u,v)

def compute_distance(obj_array):
    n = len(obj_array)
    violated = False
    if n >= 2:
        for i in range(n-1):
            for j in range (i+1,n):
                violated = False
                distance = np.linalg.norm(np.array(obj_array[i].position) - np.array(obj_array[j].position))
                if distance < 2000 and distance != 0.0:
                    violated = True
                    print("SOCIAL DISTANCING is Vilated !!!")
                    print("Distance from {} to {} is {} ".format(obj_array[i].id, obj_array[j].id, distance))
    return violated


def main():

    if (len(sys.argv) > 1):
        if ".svo" in sys.argv[1]:
            svo_file = sys.argv[1]
            print("Getting the video from: ", svo_file)
            input_type = sl.InputType()
            input_type.set_from_svo_file(svo_file)
            init_params = sl.InitParameters(input_t=input_type, svo_real_time_mode=False)   
        else:
            print("[Error]: Please specify an svo file path")
            exit(1)

    else:
            # Create a InitParameters object and set configuration parameters
        init_params = sl.InitParameters()
        init_params.camera_resolution = sl.RESOLUTION.HD720  # Use HD720 video mode
        init_params.camera_fps = 15  # Set fps at 15

    # Create a Camera object
    zed = sl.Camera()


    # Open the camera
    err = zed.open(init_params)
    if err != sl.ERROR_CODE.SUCCESS:
        exit(1)

    # Capture 50 frames and stop
    mat = sl.Mat()
    runtime_parameters = sl.RuntimeParameters()
    key = ''

    #Enable positional tracking with default parameters
    tracking_param = sl.PositionalTrackingParameters()
    err = zed.enable_positional_tracking(tracking_param)
    if err != sl.ERROR_CODE.SUCCESS:
        print("Enabled position_tracking", err, "\nExit program.")
        zed.close()
        exit(-1)

    obj_param = sl.ObjectDetectionParameters()
    obj_param.enable_tracking = True

    err = zed.enable_object_detection(obj_param)
    if err != sl.ERROR_CODE.SUCCESS:
        print("Enabled object_detection", err, "\nExit program.")
        zed.close()
        exit(-1)



    objects = sl.Objects()
    obj_runtime_param = sl.ObjectDetectionRuntimeParameters()
    obj_runtime_param.detection_confidence_threshold = 50

    corners = [0]*8
    font = cv2.FONT_HERSHEY_SIMPLEX

    # 2D Tracker for Birdeyeview
    
    camera_infos = zed.get_camera_information()
    resolution = camera_infos.camera_resolution
    camera_parameters = zed.get_camera_information(resolution).calibration_parameters.left_cam 
    
    track_view_generator = TrackingViewer()
    track_view_generator.setZMin(-1000.0 * init_params.depth_maximum_distance)
    track_view_generator.setFPS(camera_infos.camera_configuration.camera_fps, True)
    track_view_generator.setCameraCalibration(camera_infos.camera_configuration.calibration_parameters)



    while key != 113: # for 'q' key
        # Grab an image, a RuntimeParameters object must be given to grab()
        if zed.grab(runtime_parameters) == sl.ERROR_CODE.SUCCESS:
            # A new image is available if grab() returns SUCCESS
            zed.retrieve_image(mat, sl.VIEW.LEFT)
            zed.retrieve_objects(objects, obj_runtime_param)
            
            if objects.is_new:
                obj_array = objects.object_list
                print("\n"+str(len(obj_array))+ " Object are detected")
                violated = compute_distance(obj_array) 

                image_data = mat.get_data()
                for object in obj_array:
                    #object = sl.ObjectData()
                    objects.get_object_data_from_id(object,0)
                    #print("{}  {}".format(object.id, object.position))
                    bounding_box = object.bounding_box_2d
                    #bounding_box_3D = object.bounding_box
                    #print(bounding_box_3D)

                    if violated:
                        cv2.rectangle(image_data, (int(bounding_box[0,0]),int(bounding_box[0,1])),
                           (int(bounding_box[2,0]),int(bounding_box[2,1])),
                              red_color, 10)
                        cv2.putText(image_data,'SOCIAL DISTANCING VIOLATED!!!',(30,70),font,2,red_color,2,cv2.LINE_AA)
                        cv2.putText(image_data,"ID: "+str(object.id), (int(bounding_box[0,0]), int(bounding_box[0,1])),
                            font,1, red_color,2,cv2.LINE_AA)
                    
                    else:
                        cv2.rectangle(image_data, (int(bounding_box[0,0]),int(bounding_box[0,1])),
                           (int(bounding_box[2,0]),int(bounding_box[2,1])),
                              get_color_id_gr(int(object.id)), 3)
                        cv2.putText(image_data,"ID: "+str(object.id), (int(bounding_box[0,0]), int(bounding_box[0,1])),
                            font,1, get_color_id_gr(int(object.id)),2,cv2.LINE_AA)
                    
                    """
                    bounding_box_3D = object.bounding_box
                    for i in range(8):
                        corners[i] = project3Dto2D(bounding_box_3D[i])
                    # Connect top corners
                    cv2.line(image_data,corners[0],corners[1], get_color_id_gr(int(object.id)),3)
                    cv2.line(image_data,corners[1],corners[2], get_color_id_gr(int(object.id)),3)
                    cv2.line(image_data,corners[2],corners[3], get_color_id_gr(int(object.id)),3)    
                    cv2.line(image_data,corners[0],corners[3], get_color_id_gr(int(object.id)),3)    

                    # Connect bottoms corners
                    cv2.line(image_data,corners[4],corners[5], get_color_id_gr(int(object.id)),3)
                    cv2.line(image_data,corners[5],corners[6], get_color_id_gr(int(object.id)),3)
                    cv2.line(image_data,corners[6],corners[7], get_color_id_gr(int(object.id)),3)    
                    cv2.line(image_data,corners[4],corners[7], get_color_id_gr(int(object.id)),3)    

                    # Connest vertical corners
                    cv2.line(image_data,corners[0],corners[4], get_color_id_gr(int(object.id)),3)
                    cv2.line(image_data,corners[1],corners[5], get_color_id_gr(int(object.id)),3)
                    cv2.line(image_data,corners[2],corners[6], get_color_id_gr(int(object.id)),3)    
                    cv2.line(image_data,corners[3],corners[7], get_color_id_gr(int(object.id)),3)    
                    """

                cv2.imshow("ZED", image_data)
        key = cv2.waitKey(5)

    cv2.destroyAllWindows()

    # Close the camera
    zed.close()

if __name__ == "__main__":

    """
    parser = argparse.ArgumentParser()
    parser.add_argment('--svo')
    args = parser.parse_args()
    """
    main()
