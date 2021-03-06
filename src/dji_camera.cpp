/**
MIT License

Copyright (c) 2019 Michail Kalaitzakis (Unmanned Systems and Robotics Lab, 
University of South Carolina, USA)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "dji_gimbal_cam/dji_camera.h"

// OpenCV includes
#include "opencv2/imgproc/imgproc.hpp"

// ROS includes
#include "camera_info_manager/camera_info_manager.h"

// STD includes
#include <cstring>

dji_camera::dji_camera(ros::NodeHandle& nh, image_transport::ImageTransport& imageT)
{
	// Load camera parameters
	if (!loadCameraInfo())
		ROS_ERROR("Camera calibration file could not be found");

	// Initialize DJI camera
	int ret = manifold_cam_init(mode);

	if(ret == -1)
		ROS_ERROR("Camera could not be initialized");

	// Set up Publishers
	imagePub = imageT.advertise("dji_camera/image_raw", 1);
	cameraInfoPub = nh.advertise<sensor_msgs::CameraInfo>("dji_camera/camera_info", 10);

	// Initialize variables
	nCount = 0;
	imageWidth = 1280;
	imageHeight = 720;
	imageChannels = 3;
	frameSize = imageWidth * imageHeight * imageChannels / 2;
}

dji_camera::~dji_camera()
{
	// Make sure camera exits properly otherwise the connection will appear occupied
	while(!manifold_cam_exit())
		sleep(1);
}

bool dji_camera::loadCameraInfo()
{
	// Create private nodeHandle to load the camera info
	ros::NodeHandle nh_local("");

	// Initialize parameters
	std::string camera_name;
	std::string camera_info_url;
	bool transfer;

	nh_local.param("is_mono", is_mono, true);
	nh_local.param("transfer", transfer, true);
	nh_local.param("camera_name", camera_name, std::string("camera_dji"));
	nh_local.param("camera_info_url", camera_info_url,
		std::string("package://dji_camera/calibration_files/zenmuse_x3.yaml"));
	nh_local.param("camera_frame_id", camera_frame_id, std::string("dji_camera"));

	// Configure dji camera mode
	mode = GETBUFFER_MODE;

	if(transfer)
		mode |= TRANSFER_MODE;

	// Create camera info manager
	camera_info_manager::CameraInfoManager camInfoMngr(nh_local, camera_name, camera_info_url);

	if (camInfoMngr.loadCameraInfo(camera_info_url))
	{
		cam_info = camInfoMngr.getCameraInfo();
		ROS_INFO("Camera calibration file loaded");
	}
	else
		return false;

	return true;
}

bool dji_camera::grabFrame()
{
	// Check if new frame is available
	unsigned char buffer[frameSize];
	unsigned int nFrame;
	int ret = manifold_cam_read(buffer, &nFrame, 1);

	if(ret == -1)
		return false;
	else
	{
		// Create openCV Mat to store the image
		cv::Mat frameYcbCr = cv::Mat(imageHeight * 3 / 2, imageWidth, CV_8UC1, buffer);
		cv::Mat frameBGRGray;

		// If grayscale image is selected
		if (is_mono)
			cv::cvtColor(frameYcbCr, frameBGRGray, CV_YUV2GRAY_NV12);
		// If coloured image is selected
		else
			cv::cvtColor(frameYcbCr, frameBGRGray, CV_YUV2BGR_NV12);
		//cv::resize(frameBGRGray,frameBGRGray,cv::Size(),.5,.5);
		rosMat.image = frameBGRGray;
	}
	return true;
}

bool dji_camera::publishAll()
{

	// Check if new frame was captured
	if(grabFrame())
	{
		// Get time
		ros::Time time = ros::Time::now();

		// Setup image message
		sensor_msgs::Image rosImage;

		if(is_mono)
			rosMat.encoding = "mono8";
		else
			rosMat.encoding = "bgr8";

		rosMat.header.stamp = time;
		rosMat.header.frame_id = camera_frame_id;

		rosMat.toImageMsg(rosImage);

		// Setup camera info message
		cam_info.header.stamp = time;
		cam_info.header.seq = nCount;
		cam_info.header.frame_id = camera_frame_id;

		// Publish everything
		imagePub.publish(rosImage);
		cameraInfoPub.publish(cam_info);

		nCount++;

		return true;
	}
	else
		return false;
}

////////////////////////////////////////////////////////////
////////////////////////  Main  ////////////////////////////
////////////////////////////////////////////////////////////

int main(int argc, char** argv)
{
  ros::init(argc, argv, "dji_camera_node");
  ros::NodeHandle nh;
  image_transport::ImageTransport imageT(nh);

  dji_camera manifoldCamera(nh, imageT);
  ros::Rate r = ros::Rate(30);
  while(ros::ok() && !manifold_cam_exit())
  {
  	ros::spinOnce();

  	if(!manifoldCamera.publishAll())
  	{
  		ROS_ERROR("Could not retrieve new frame");
  		break;
  	}
	r.sleep();
  }

  return 0;
}
