/*
 * @Description: 负责收取qrcode的信息， 设置导航目标点
 * @Author: lifuguan
 * @Date: 2019-10-03 17:21:04
 * @LastEditTime: 2019-10-18 15:37:59
 * @LastEditors: Please set LastEditors
 */

#include <iostream>
#include <ros/ros.h>
#include <tf/transform_broadcaster.h>
#include <tf/transform_listener.h>
#include <geometry_msgs/Twist.h>
#include <std_msgs/String.h>
#include <gx2019_omni_simulations/arm_transport.h>
#include <gx2019_omni_simulations/cv_mission_type.h>

using namespace std;

tf::Transform goals[3][4] =
    {
        // 成品区
        {tf::Transform(tf::Quaternion(0, 0, 1.0, 1), tf::Vector3(2.0, -0.86, 0)),
         tf::Transform(tf::Quaternion(0, 0, 1.0, 1), tf::Vector3(2.0, -1.05, 0)),
         tf::Transform(tf::Quaternion(0, 0, 1.0, 1), tf::Vector3(2.0, -1.25, 0))},
        // 加工区
        {tf::Transform(tf::Quaternion(0, 0, 1.0, 1), tf::Vector3(1.36, -2.0, 0)),
         tf::Transform(tf::Quaternion(0, 0, 1.0, 1), tf::Vector3(1.51, -2.0, 0)),
         tf::Transform(tf::Quaternion(0, 0, 1.0, 1), tf::Vector3(1.68, -2.0, 0))},
        // 物料区 从右往左
        {tf::Transform(tf::Quaternion(0, 0, 1.0, 1), tf::Vector3(0.3, -1.05, 0)),
         tf::Transform(tf::Quaternion(0, 0, 1.0, 1), tf::Vector3(0.3, -1.20, 0)),
         tf::Transform(tf::Quaternion(0, 0, 1.0, 1), tf::Vector3(0.3, -1.35, 0))}};

tf::Transform inital_point(tf::Quaternion(0, 0, 0, 1), tf::Vector3(-0.2, -0.2, 0));
tf::Transform qrcode_point(tf::Quaternion(0, 0, 1.0, 1), tf::Vector3(0.8, -1.8, 0));

int qrcode_message[2][4] = {0};

int material_queue[4];
int step = 0;

bool check_if_mission_reviced = false;

geometry_msgs::Twist cmd_vel;
// 接受二维码任务信息
void qrcodeMessageCallback(const std_msgs::String qrcode_message_);
// 接收色块队列信息并与坐标信息对应
void cvMaterialQueueSub(const gx2019_omni_simulations::cv_mission_type &msg);
// 速度计算函数
double cmdVelCalculate(tf::StampedTransform goal_to_car_stamped);

int main(int argc, char **argv)
{
    ros::init(argc, argv, "gx2019_mission_control_node");
    ros::NodeHandle nh;
    ros::Subscriber sub = nh.subscribe<std_msgs::String>("qrcode_message", 10, qrcodeMessageCallback);
    ros::Publisher pub = nh.advertise<geometry_msgs::Twist>("/cmd_vel", 10);
    ros::Publisher cv_mission_pub = nh.advertise<gx2019_omni_simulations::cv_mission_type>("cv_mission_topic", 100);
    ros::Subscriber cv_material_queue_sub = nh.subscribe("cv_material_queue", 5, cvMaterialQueueSub);
    tf::TransformBroadcaster goal_frame_broadcaster;
    tf::TransformListener goal_to_car_listener;
    tf::StampedTransform goal_to_car_stamped;

    gx2019_omni_simulations::cv_mission_type cv_mission_type;

    ros::Rate rate(5.0);
    while (nh.ok())
    {
        // 识别到二维码之前（正在前往）
        if (qrcode_message[0][0] == 0)
        {
            goal_frame_broadcaster.sendTransform(tf::StampedTransform(qrcode_point, ros::Time::now(), "goal", "map"));

            //避免第一次订阅时翻车
            try
            {
                goal_to_car_listener.lookupTransform("goal", "base_link", ros::Time(0), goal_to_car_stamped);

                double dst = cmdVelCalculate(goal_to_car_stamped);
                ROS_INFO("%f", dst);
                // 如果没有达到目标距离 / 没有收到队列信息
                if (dst <= 0.7 && dst >= 0.5 && check_if_mission_reviced == false)
                {
                    // 通讯， 打开识别
                    cv_mission_type.cv_mission_type = 1;
                }
                else
                {
                    // 啥都不做
                    cv_mission_type.cv_mission_type = 0;
                }
                cv_mission_pub.publish(cv_mission_type);
            }
            catch (tf::TransformException &ex)
            {
                ROS_ERROR("%s", ex.what());
                ros::Duration(1.0).sleep();
                cmd_vel.linear.x = 0;
                cmd_vel.linear.y = 0;
            }
        }
        //识别到二维码之后
        else
        {
            if (step == 0)
            {
                goal_frame_broadcaster.sendTransform(tf::StampedTransform(goals[2][material_queue[qrcode_message[0][0] - 1]], ros::Time::now(), "goal", "map"));
                goal_to_car_listener.lookupTransform("goal", "base_link", ros::Time(0), goal_to_car_stamped);
                double dst = sqrt(pow(goal_to_car_stamped.getOrigin().x(), 2) + pow(goal_to_car_stamped.getOrigin().y(), 2));
                // 开启跟踪
                if (dst <= 0.5 && dst >= 0.05)
                {
                    cv_mission_type.cv_mission_type = 2;
                }
                // 抓物体
                else if (dst <= 0.05)
                {
                    cv_mission_type.cv_mission_type = 3;
                    // 延时5s，视情况而定，机械臂抓取物体
                    ros::Duration(5).sleep();

                }
                cv_mission_pub.publish(cv_mission_type);
            }
            

            cmdVelCalculate(goal_to_car_stamped);
        }


        pub.publish(cmd_vel);
        ros::spinOnce();
        rate.sleep();
    }

    return 0;
}

void qrcodeMessageCallback(const std_msgs::String qrcode_message_)
{
    for (int i = 0; i < 3; i++)
    {
        qrcode_message[0][i] = qrcode_message_.data[i] - 48;
        qrcode_message[1][i] = qrcode_message_.data[4 + i] - 48;
    }
}

// 接收色块队列信息并与坐标信息对应
void cvMaterialQueueSub(const gx2019_omni_simulations::cv_mission_type &msg)
{
    for (int i = 0; i < msg.material_queue.size(); i++)
    {
        material_queue[msg.material_queue[i]] = i;
        check_if_mission_reviced = true;
    }
}

double cmdVelCalculate(tf::StampedTransform goal_to_car_stamped)
{
    //四元数转RPY ， 使用yaw
    double yaw, pitch, roll;
    tf::Matrix3x3(goal_to_car_stamped.getRotation()).getEulerYPR(yaw, pitch, roll);

    double x, y, rotation, dst;
    x = -goal_to_car_stamped.getOrigin().y();
    y = goal_to_car_stamped.getOrigin().x();
    rotation = yaw;
    dst = sqrt(pow(x, 2) + pow(y, 2));
    if (dst <= 0.02)
    {
        cmd_vel.linear.x = 0;
        cmd_vel.linear.y = 0;
        cmd_vel.angular.z = 0;
    }
    else
    {
        cmd_vel.linear.x = 0.3 * tanh(dst) * (x / dst);
        cmd_vel.linear.y = 0.3 * tanh(dst) * (y / dst);
        // cmd_vel.angular.z = -0.7 * rotation;
        // cmd_vel.angular.z = atan2(x, y);
    }

    return dst;
}