// Copyright 2013, Ji Zhang, Carnegie Mellon University
// Further contributions copyright (c) 2016, Southwest Research Institute
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from this
//    software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// This is an implementation of the algorithm described in the following paper:
//   J. Zhang and S. Singh. LOAM: Lidar Odometry and Mapping in Real-time.
//     Robotics: Science and Systems Conference (RSS). Berkeley, CA, July 2014.

#include "utility.h"
#include "Eigen/Core"
#include "Eigen/Geometry"
#include "bdxt/rtk.h"
#include <iostream>

class TransformFusion{

private:

    ros::NodeHandle nh;

    ros::Publisher pubLaserOdometry2;
    ros::Subscriber subLaserOdometry;
    ros::Subscriber subOdomAftMapped;
    ros::Subscriber subRTK;

    nav_msgs::Odometry laserOdometry2;
    tf::StampedTransform laserOdometryTrans2;
    tf::TransformBroadcaster tfBroadcaster2;

    tf::StampedTransform map_2_camera_init_Trans;
    tf::TransformBroadcaster tfBroadcasterMap2CameraInit;

    tf::StampedTransform camera_2_base_link_Trans;
    tf::TransformBroadcaster tfBroadcasterCamera2Baselink;

    ros::Publisher pubENU; // 发布东北天的坐标

    float transformSum[6];
    float transformIncre[6];
    float transformMapped[6];
    float transformBefMapped[6];
    float transformAftMapped[6];

    std_msgs::Header currentHeader;

    bool is_gps_init; 
    Eigen::Isometry3d tBodyInit_in_ENU;

public:

    TransformFusion(){

        pubLaserOdometry2 = nh.advertise<nav_msgs::Odometry> ("/integrated_to_init", 5);
        subLaserOdometry = nh.subscribe<nav_msgs::Odometry>("/laser_odom_to_init", 5, &TransformFusion::laserOdometryHandler, this);
        subOdomAftMapped = nh.subscribe<nav_msgs::Odometry>("/aft_mapped_to_init", 5, &TransformFusion::odomAftMappedHandler, this);
        subRTK = nh.subscribe<bdxt::rtk>("/RTK",5,&TransformFusion::rtkHandler, this);
        pubENU = nh.advertise<nav_msgs::Odometry>("/ENU",5);

        laserOdometry2.header.frame_id = "/camera_init";
        laserOdometry2.child_frame_id = "/camera";

        laserOdometryTrans2.frame_id_ = "/camera_init";
        laserOdometryTrans2.child_frame_id_ = "/camera";

        map_2_camera_init_Trans.frame_id_ = "/map";
        map_2_camera_init_Trans.child_frame_id_ = "/camera_init";

        camera_2_base_link_Trans.frame_id_ = "/camera";
        camera_2_base_link_Trans.child_frame_id_ = "/base_link";

        is_gps_init = false;
        tBodyInit_in_ENU = Eigen::Isometry3d::Identity();

        for (int i = 0; i < 6; ++i)
        {
            transformSum[i] = 0;
            transformIncre[i] = 0;
            transformMapped[i] = 0;
            transformBefMapped[i] = 0;
            transformAftMapped[i] = 0;
        }
    }

    void transformAssociateToMap()
    {
        float x1 = cos(transformSum[1]) * (transformBefMapped[3] - transformSum[3]) 
                 - sin(transformSum[1]) * (transformBefMapped[5] - transformSum[5]);
        float y1 = transformBefMapped[4] - transformSum[4];
        float z1 = sin(transformSum[1]) * (transformBefMapped[3] - transformSum[3]) 
                 + cos(transformSum[1]) * (transformBefMapped[5] - transformSum[5]);

        float x2 = x1;
        float y2 = cos(transformSum[0]) * y1 + sin(transformSum[0]) * z1;
        float z2 = -sin(transformSum[0]) * y1 + cos(transformSum[0]) * z1;

        transformIncre[3] = cos(transformSum[2]) * x2 + sin(transformSum[2]) * y2;
        transformIncre[4] = -sin(transformSum[2]) * x2 + cos(transformSum[2]) * y2;
        transformIncre[5] = z2;

        float sbcx = sin(transformSum[0]);
        float cbcx = cos(transformSum[0]);
        float sbcy = sin(transformSum[1]);
        float cbcy = cos(transformSum[1]);
        float sbcz = sin(transformSum[2]);
        float cbcz = cos(transformSum[2]);

        float sblx = sin(transformBefMapped[0]);
        float cblx = cos(transformBefMapped[0]);
        float sbly = sin(transformBefMapped[1]);
        float cbly = cos(transformBefMapped[1]);
        float sblz = sin(transformBefMapped[2]);
        float cblz = cos(transformBefMapped[2]);

        float salx = sin(transformAftMapped[0]);
        float calx = cos(transformAftMapped[0]);
        float saly = sin(transformAftMapped[1]);
        float caly = cos(transformAftMapped[1]);
        float salz = sin(transformAftMapped[2]);
        float calz = cos(transformAftMapped[2]);

        float srx = -sbcx*(salx*sblx + calx*cblx*salz*sblz + calx*calz*cblx*cblz)
                  - cbcx*sbcy*(calx*calz*(cbly*sblz - cblz*sblx*sbly)
                  - calx*salz*(cbly*cblz + sblx*sbly*sblz) + cblx*salx*sbly)
                  - cbcx*cbcy*(calx*salz*(cblz*sbly - cbly*sblx*sblz) 
                  - calx*calz*(sbly*sblz + cbly*cblz*sblx) + cblx*cbly*salx);
        transformMapped[0] = -asin(srx);

        float srycrx = sbcx*(cblx*cblz*(caly*salz - calz*salx*saly)
                     - cblx*sblz*(caly*calz + salx*saly*salz) + calx*saly*sblx)
                     - cbcx*cbcy*((caly*calz + salx*saly*salz)*(cblz*sbly - cbly*sblx*sblz)
                     + (caly*salz - calz*salx*saly)*(sbly*sblz + cbly*cblz*sblx) - calx*cblx*cbly*saly)
                     + cbcx*sbcy*((caly*calz + salx*saly*salz)*(cbly*cblz + sblx*sbly*sblz)
                     + (caly*salz - calz*salx*saly)*(cbly*sblz - cblz*sblx*sbly) + calx*cblx*saly*sbly);
        float crycrx = sbcx*(cblx*sblz*(calz*saly - caly*salx*salz)
                     - cblx*cblz*(saly*salz + caly*calz*salx) + calx*caly*sblx)
                     + cbcx*cbcy*((saly*salz + caly*calz*salx)*(sbly*sblz + cbly*cblz*sblx)
                     + (calz*saly - caly*salx*salz)*(cblz*sbly - cbly*sblx*sblz) + calx*caly*cblx*cbly)
                     - cbcx*sbcy*((saly*salz + caly*calz*salx)*(cbly*sblz - cblz*sblx*sbly)
                     + (calz*saly - caly*salx*salz)*(cbly*cblz + sblx*sbly*sblz) - calx*caly*cblx*sbly);
        transformMapped[1] = atan2(srycrx / cos(transformMapped[0]), 
                                   crycrx / cos(transformMapped[0]));
        
        float srzcrx = (cbcz*sbcy - cbcy*sbcx*sbcz)*(calx*salz*(cblz*sbly - cbly*sblx*sblz)
                     - calx*calz*(sbly*sblz + cbly*cblz*sblx) + cblx*cbly*salx)
                     - (cbcy*cbcz + sbcx*sbcy*sbcz)*(calx*calz*(cbly*sblz - cblz*sblx*sbly)
                     - calx*salz*(cbly*cblz + sblx*sbly*sblz) + cblx*salx*sbly)
                     + cbcx*sbcz*(salx*sblx + calx*cblx*salz*sblz + calx*calz*cblx*cblz);
        float crzcrx = (cbcy*sbcz - cbcz*sbcx*sbcy)*(calx*calz*(cbly*sblz - cblz*sblx*sbly)
                     - calx*salz*(cbly*cblz + sblx*sbly*sblz) + cblx*salx*sbly)
                     - (sbcy*sbcz + cbcy*cbcz*sbcx)*(calx*salz*(cblz*sbly - cbly*sblx*sblz)
                     - calx*calz*(sbly*sblz + cbly*cblz*sblx) + cblx*cbly*salx)
                     + cbcx*cbcz*(salx*sblx + calx*cblx*salz*sblz + calx*calz*cblx*cblz);
        transformMapped[2] = atan2(srzcrx / cos(transformMapped[0]), 
                                   crzcrx / cos(transformMapped[0]));

        x1 = cos(transformMapped[2]) * transformIncre[3] - sin(transformMapped[2]) * transformIncre[4];
        y1 = sin(transformMapped[2]) * transformIncre[3] + cos(transformMapped[2]) * transformIncre[4];
        z1 = transformIncre[5];

        x2 = x1;
        y2 = cos(transformMapped[0]) * y1 - sin(transformMapped[0]) * z1;
        z2 = sin(transformMapped[0]) * y1 + cos(transformMapped[0]) * z1;

        transformMapped[3] = transformAftMapped[3] 
                           - (cos(transformMapped[1]) * x2 + sin(transformMapped[1]) * z2);
        transformMapped[4] = transformAftMapped[4] - y2;
        transformMapped[5] = transformAftMapped[5] 
                           - (-sin(transformMapped[1]) * x2 + cos(transformMapped[1]) * z2);
    }

    void laserOdometryHandler(const nav_msgs::Odometry::ConstPtr& laserOdometry)
    {
        currentHeader = laserOdometry->header;
        // lego loam中的roll,pitch,yaw定义和ROS不同
        // lego loam中pitch对应x，yaw对应y，roll对应z
        // ros中的roll对应x,pitch对应y,yaw对应z
        double roll, pitch, yaw;
        geometry_msgs::Quaternion geoQuat = laserOdometry->pose.pose.orientation;
        tf::Matrix3x3(tf::Quaternion(geoQuat.z, -geoQuat.x, -geoQuat.y, geoQuat.w)).getRPY(roll, pitch, yaw);

        transformSum[0] = -pitch; // tfs[0] = -pitch = -(-geoQuat.x) = orientation.x
        transformSum[1] = -yaw;   // tfs[1] = -yaw   = -(-geoQuat.y) = orientation.y
        transformSum[2] = roll;   // tfs[2] = -roll  =    geoQuat.z  = orientation.z

        transformSum[3] = laserOdometry->pose.pose.position.x;
        transformSum[4] = laserOdometry->pose.pose.position.y;
        transformSum[5] = laserOdometry->pose.pose.position.z;

        // 融合地图的xyzrpy
        transformAssociateToMap();

        geoQuat = tf::createQuaternionMsgFromRollPitchYaw
                  (transformMapped[2], -transformMapped[0], -transformMapped[1]);

        laserOdometry2.header.stamp = laserOdometry->header.stamp;
        laserOdometry2.pose.pose.orientation.x = -geoQuat.y;
        laserOdometry2.pose.pose.orientation.y = -geoQuat.z;
        laserOdometry2.pose.pose.orientation.z = geoQuat.x;
        laserOdometry2.pose.pose.orientation.w = geoQuat.w;
        laserOdometry2.pose.pose.position.x = transformMapped[3];
        laserOdometry2.pose.pose.position.y = transformMapped[4];
        laserOdometry2.pose.pose.position.z = transformMapped[5];
        pubLaserOdometry2.publish(laserOdometry2);

        laserOdometryTrans2.stamp_ = laserOdometry->header.stamp;
        laserOdometryTrans2.setRotation(tf::Quaternion(-geoQuat.y, -geoQuat.z, geoQuat.x, geoQuat.w));
        laserOdometryTrans2.setOrigin(tf::Vector3(transformMapped[3], transformMapped[4], transformMapped[5]));
        tfBroadcaster2.sendTransform(laserOdometryTrans2);

        // *********************************    激光里程计 --> 全局位姿    ***********************************
        // 0. 激光里程计是当前激光传感器相对于激光第一帧的变换，但是我们要的是当前车体相对于车体第一帧的变换，这样才能得到车体在东北天
        //    坐标系下的位姿。
        // 1. 激光 --> 车体系：利用激光到激光第一帧和激光第一帧到车体第一帧的关系求得。
        // 2. 车体 --> 车体系：利用激光到车体第一帧和激光到车体的关系得到。
        // 2. 车体系 --> 东北天：坐标变换，利用第一帧的xyzrpy变换得到。
        // ************************************************************************************************
        if(!is_gps_init){
            ROS_INFO("gps_not_init!");
            return;
        }

        Eigen::Isometry3d tLidar_in_LidarInit = Eigen::Isometry3d::Identity(); // 将当前的位姿赋值给mLidar
        tLidar_in_LidarInit.translate(Eigen::Vector3d(
                            laserOdometry2.pose.pose.position.x,
                        laserOdometry2.pose.pose.position.y,
                    laserOdometry2.pose.pose.position.z
        ));
        tLidar_in_LidarInit.rotate(Eigen::Quaterniond(
                            laserOdometry2.pose.pose.orientation.z,
                        -laserOdometry2.pose.pose.orientation.x,
                    -laserOdometry2.pose.pose.orientation.y,
                laserOdometry2.pose.pose.orientation.w
        ));

        // 1. mLidar --> mBodyInit
        Eigen::Isometry3d tLidarInit_in_BodyInit = Eigen::Isometry3d::Identity();
        tLidarInit_in_BodyInit.translate(Eigen::Vector3d(1.83,0,0));
        Eigen::Isometry3d tLidar_in_BodyInit = tLidarInit_in_BodyInit*tLidar_in_LidarInit;

        // 2. mBody --> mBodyInit
        Eigen::Isometry3d tBody_in_Lidar = Eigen::Isometry3d::Identity();
        tBody_in_Lidar.translate(Eigen::Vector3d(-1.83,0,0));
        Eigen::Isometry3d tBody_in_BodyInit = tLidar_in_BodyInit*tBody_in_Lidar;

        // 3. mBody --> ENU
        Eigen::Isometry3d mENU = tBodyInit_in_ENU*tBody_in_BodyInit;

        // 3. 发布当前的ENU坐标
        Eigen::Vector3d vEulerENU_ = mENU.rotation().eulerAngles(2,1,0);
        Eigen::Quaterniond qEulerENU_ = Eigen::Quaterniond(mENU.rotation());
        Eigen::Vector3d vPoseENU_ = mENU.translation();
        nav_msgs::Odometry msgENU;
        msgENU.header.stamp = laserOdometry->header.stamp;
        msgENU.pose.pose.position.x = vPoseENU_.x();
        msgENU.pose.pose.position.y = vPoseENU_.y();
        msgENU.pose.pose.position.z = vPoseENU_.z();
        msgENU.pose.pose.orientation.x = -qEulerENU_.y();
        msgENU.pose.pose.orientation.y = -qEulerENU_.z();
        msgENU.pose.pose.orientation.z = qEulerENU_.x();
        msgENU.pose.pose.orientation.w = qEulerENU_.w();
        pubENU.publish(msgENU);
    }

    void rtkHandler(const bdxt::rtk::ConstPtr& RTK)
    {
        if(!is_gps_init){ // gps初始值记录
            if(RTK->navStatus == 4 && RTK->rtkStatus == 5){
                // qBody2ENU计算车体坐标系到gps(东北天)坐标系的旋转
                // 而可以获取的imu读数是惯导系下的值(北东地)，因此需要作变换：BODY-->北东地-->东北天
                // 涉及两个四元数：qBODY_in_NED,qNED_in_ENU
                Eigen::Vector3d vBody_in_ENU; // gps初始化位姿：x,y,z,pitch,yaw,roll，同时也是激光原点坐标系和大地坐标系的静态变换
                Eigen::Quaterniond qBody_in_ENU; // body到ENU的旋转变换

                // RTK的RPY测量的是车体相对于ENU坐标系的旋转
                // x_in_y这里表示的是，x坐标系在y坐标系下的表示，也就是y坐标系到x坐标系的变换
                Eigen::Quaterniond qBody_in_NED;
                qBody_in_NED = Eigen::AngleAxisd(RTK->yaw*PI/180.0,Eigen::Vector3d::UnitZ())*
                            Eigen::AngleAxisd(RTK->pitch*PI/180.0,Eigen::Vector3d::UnitY())*
                            Eigen::AngleAxisd(RTK->roll*PI/180.0,Eigen::Vector3d::UnitX());
                // 北东地变换到东北天，先z轴旋转PI/2，再x轴旋转PI，注意顺序
                Eigen::Quaterniond qENU_in_NED; // 其实是NED坐标系转ENU坐标系
                qENU_in_NED = Eigen::AngleAxisd(PI/2,Eigen::Vector3d::UnitZ())*
                        Eigen::AngleAxisd(PI,Eigen::Vector3d::UnitX());
                // 我们要用东北天到北东地，因此取反
                qBody_in_ENU = qENU_in_NED.conjugate()*qBody_in_NED;
                
                // Body坐标系在ENU坐标系下的位置，直接读gps转换函数的读数
                vBody_in_ENU[0] = RTK->x;  // 如果使用第一帧为坐标原点坐标转换，设置为0
                vBody_in_ENU[1] = RTK->y;  // 如果使用第一帧为坐标原点坐标转换，设置为0
                vBody_in_ENU[2] = RTK->Ati;  // 如果使用第一帧为坐标原点坐标转换，设置为0

                // 得到变换矩阵，先按ENU坐标轴平移，再旋转
                tBodyInit_in_ENU.translate(vBody_in_ENU);
                tBodyInit_in_ENU.rotate(qBody_in_ENU);

                std::cout<<tBodyInit_in_ENU.matrix()<<std::endl;
                ROS_INFO("gps init successfully!\n");
                is_gps_init = true;
            }
            else{
                ROS_INFO("bad RTK status!\n");
            }
        }
        // TODO: 注意激光的时间戳不在这里，时间戳判断无法进行，这部分功能需要放到imageProjection.cpp中
        // 目前只需要第一帧的坐标变换，不需要时间戳
        // vins fusion中将gps和vio里程计的时间戳进行对准，而非camera的时间戳。这样看是有问题的
    }

    void odomAftMappedHandler(const nav_msgs::Odometry::ConstPtr& odomAftMapped)
    {
        double roll, pitch, yaw;
        geometry_msgs::Quaternion geoQuat = odomAftMapped->pose.pose.orientation;
        tf::Matrix3x3(tf::Quaternion(geoQuat.z, -geoQuat.x, -geoQuat.y, geoQuat.w)).getRPY(roll, pitch, yaw);

        transformAftMapped[0] = -pitch;
        transformAftMapped[1] = -yaw;
        transformAftMapped[2] = roll;

        transformAftMapped[3] = odomAftMapped->pose.pose.position.x;
        transformAftMapped[4] = odomAftMapped->pose.pose.position.y;
        transformAftMapped[5] = odomAftMapped->pose.pose.position.z;

        transformBefMapped[0] = odomAftMapped->twist.twist.angular.x;
        transformBefMapped[1] = odomAftMapped->twist.twist.angular.y;
        transformBefMapped[2] = odomAftMapped->twist.twist.angular.z;

        transformBefMapped[3] = odomAftMapped->twist.twist.linear.x;
        transformBefMapped[4] = odomAftMapped->twist.twist.linear.y;
        transformBefMapped[5] = odomAftMapped->twist.twist.linear.z;
    }
};




int main(int argc, char** argv)
{
    ros::init(argc, argv, "lego_loam");
    
    TransformFusion TFusion;

    ROS_INFO("\033[1;32m---->\033[0m Transform Fusion Started.");

    ros::spin();

    return 0;
}
