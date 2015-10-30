/**
 * @file cartesian_planner.cpp
 * @brief Cartesian path planner for moveit.
 *
 * This class is used to represent a cartesian path planner for moveit.
 * It finds a straight line path between the start and goal position. This
 * planner does not have the inherent ability to avoid collision. It does
 * check if the path created is collision free before it returns a trajectory.
 * If a collision is found it returns an empty trajectory and moveit error.
 *
 * @author Levi Armstrong
 * @date May 4, 2015
 * @version TODO
 * @bug No known bugs
 *
 * @copyright Copyright (c) 2015, Southwest Research Institute
 *
 * @license Software License Agreement (Apache License)\n
 * \n
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at\n
 * \n
 * http://www.apache.org/licenses/LICENSE-2.0\n
 * \n
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <constrained_ik/moveit_interface/cartesian_planner.h>
#include <ros/ros.h>
#include <eigen3/Eigen/Core>
#include <eigen_conversions/eigen_msg.h>
#include <moveit/robot_state/conversions.h>
#include <constrained_ik/basic_kin.h>

namespace constrained_ik
{

  boost::shared_ptr<constrained_ik::MasterIK> CartesianPlanner::getSolver(std::string group_name)
  {
    boost::shared_ptr<constrained_ik::MasterIK> solver;
    std::map<std::string, boost::shared_ptr<constrained_ik::MasterIK> >::iterator it;
    it = solvers_.find(group_name);
    if (it==solvers_.end())
    {
      solver.reset(new constrained_ik::MasterIK(group_name));
      basic_kin::BasicKin kin;

      //initialize kinematic solver with robot info
      bool sucess = kin.init(robot_model_->getJointModelGroup(group_name));
      if (sucess)
      {
        solver->init(kin);
        solvers_.insert(std::make_pair(group_name, solver));
      }
      else
      {
        ROS_ERROR("Cartesian planner could not load solver for move_group %s", group_name.c_str());
      }
    }
    else
    {
      solver = it->second;
    }
    return solver;
  }

  bool CartesianPlanner::solve(planning_interface::MotionPlanResponse &res)
  {
    ros::WallTime start_time = ros::WallTime::now();
    robot_state::RobotStatePtr mid_state;
    std::vector<std::string> joint_names, link_names;
    Eigen::Affine3d start_pose, goal_pose;
    std::vector<double> pos(1);

    robot_model_ = planning_scene_->getRobotModel();

    robot_state::RobotState start_state(robot_model_);
    robot_state::robotStateMsgToRobotState(request_.start_state, start_state);
    robot_state::RobotState goal_state = start_state;
    robot_trajectory::RobotTrajectoryPtr traj(new robot_trajectory::RobotTrajectory(robot_model_, request_.group_name));
    boost::shared_ptr<constrained_ik::MasterIK> solver;
    const robot_model::JointModelGroup *jmg = robot_model_->getJointModelGroup(request_.group_name);

    solver = getSolver(request_.group_name);
    joint_names = jmg->getActiveJointModelNames();
    link_names = jmg->getLinkModelNames();
    start_pose = start_state.getFrameTransform(link_names.back());

    ROS_INFO_STREAM("Cartesian Planning for Group: " << request_.group_name);
    // if we have path constraints, we prefer interpolating in pose space
    if (!request_.goal_constraints[0].joint_constraints.empty())
    {
      for(unsigned int i = 0; i < request_.goal_constraints[0].joint_constraints.size(); i++)
      {
        pos[0]=request_.goal_constraints[0].joint_constraints[i].position;
        goal_state.setJointPositions(joint_names[i], pos);
      }
      goal_pose = goal_state.getFrameTransform(link_names.back());
    }
    else
    {
      geometry_msgs::Pose pose;
      if (!request_.goal_constraints[0].position_constraints.empty() && !request_.goal_constraints[0].orientation_constraints.empty())
      {
        pose.position = request_.goal_constraints[0].position_constraints[0].constraint_region.primitive_poses[0].position;
        pose.orientation = request_.goal_constraints[0].orientation_constraints[0].orientation;
      }
      else if (!request_.goal_constraints[0].position_constraints.empty() && request_.goal_constraints[0].orientation_constraints.empty())
      {
        tf::poseEigenToMsg(start_state.getFrameTransform(link_names.back()), pose);
        pose.position = request_.goal_constraints[0].position_constraints[0].constraint_region.primitive_poses[0].position;
      }
      else if (request_.goal_constraints[0].position_constraints.empty() && !request_.goal_constraints[0].orientation_constraints.empty())
      {
        tf::poseEigenToMsg(start_state.getFrameTransform(link_names.back()), pose);
        pose.orientation = request_.goal_constraints[0].orientation_constraints[0].orientation;
      }
      else
      {
        ROS_ERROR("No constraint was passed with request!");
        return false;
      }
      tf::poseMsgToEigen(pose, goal_pose);
    }

    ROS_DEBUG_NAMED("clik", "Setting Position x from %f to %f", start_pose.translation()(0),goal_pose.translation()(0));
    ROS_DEBUG_NAMED("clik", "Setting Position y from %f to %f", start_pose.translation()(1),goal_pose.translation()(1));
    ROS_DEBUG_NAMED("clik", "Setting Position z from %f to %f", start_pose.translation()(2),goal_pose.translation()(2));
    ROS_DEBUG_NAMED("clik", "Setting Position yaw   from %f to %f", start_pose.rotation().eulerAngles(3,2,1)(0),goal_pose.rotation().eulerAngles(3,2,1)(0));
    ROS_DEBUG_NAMED("clik", "Setting Position pitch from %f to %f", start_pose.rotation().eulerAngles(3,2,1)(1),goal_pose.rotation().eulerAngles(3,2,1)(1));
    ROS_DEBUG_NAMED("clik", "Setting Position roll  from %f to %f", start_pose.rotation().eulerAngles(3,2,1)(2),goal_pose.rotation().eulerAngles(3,2,1)(2));

    // Generate Interpolated Cartesian Poses
    std::vector<Eigen::Affine3d, Eigen::aligned_allocator<Eigen::Affine3d> > poses = interpolateCartesian(start_pose, goal_pose, config_.translational_discretization_step, config_.orientational_discretization_step);
    Eigen::Affine3d world_to_base = solver->getKin().getRobotBaseInWorld().inverse();

    // Generate Cartesian Trajectory
    int steps = poses.size();
    Eigen::VectorXd start_joints, joint_angles;
    bool found_ik;
    mid_state = robot_model::RobotStatePtr(new robot_model::RobotState(start_state));
    for (int j=0; j<steps; j++)
    {
      if (j!=0)
      {
        mid_state->copyJointGroupPositions(request_.group_name, start_joints);

        //Do IK and report results
        try
        {
          found_ik = solver->calcInvKin(world_to_base*poses[j], start_joints, planning_scene_, joint_angles);
          mid_state->setJointGroupPositions(request_.group_name, joint_angles);
          mid_state->update();
        }
        catch (std::exception &e)
        {
          found_ik = false;
        }

        if (!found_ik || planning_scene_->isStateColliding(*mid_state, request_.group_name))
        {
          if (!config_.debug_mode)
          {
            ROS_INFO("Cartesian planner was unable to find a valid solution. :(");
            res.error_code_.val = moveit_msgs::MoveItErrorCodes::INVALID_MOTION_PLAN;
            return false;
          }
          else
          {
            break;
          }
        }
        
        
      }
      traj->addSuffixWayPoint(*mid_state, 0.0);

      if (terminate_)
        break;
      
      res.planning_time_ = (ros::WallTime::now() - start_time).toSec();
      if (res.planning_time_ > request_.allowed_planning_time)
      {
        ROS_INFO("Cartesian planner was unable to find solution in allowed time. :(");
        res.error_code_.val = moveit_msgs::MoveItErrorCodes::TIMED_OUT;
        return false;
      }
    }

    // Check if planner was terminated
    if (terminate_)
    {
      ROS_INFO("Cartesian Trajectory was terminated!");
      res.error_code_.val = moveit_msgs::MoveItErrorCodes::INVALID_MOTION_PLAN;
      return false;
    }

    // Check if traj is a collision free path
    if (planning_scene_->isPathValid(*traj, request_.group_name))
    {
      ROS_INFO("Cartesian Trajectory is collision free! :)");
      res.trajectory_=traj;
      res.error_code_.val = moveit_msgs::MoveItErrorCodes::SUCCESS;
      return true;
    }
    else
    {
      ROS_INFO("Cartesian Trajectory is not collision free. :(");
      res.error_code_.val = moveit_msgs::MoveItErrorCodes::INVALID_MOTION_PLAN;
      return false;
    }
  }

  std::vector<Eigen::Affine3d, Eigen::aligned_allocator<Eigen::Affine3d> >
  CartesianPlanner::interpolateCartesian(const Eigen::Affine3d& start,
                                            const Eigen::Affine3d& stop,
                                            double ds, double dt) const
  {
    // Required position change
    Eigen::Vector3d delta_translation = (stop.translation() - start.translation());
    Eigen::Vector3d start_pos = start.translation();
    Eigen::Affine3d stop_prime = start.inverse()*stop; //This the stop pose represented in the start pose coordinate system
    Eigen::AngleAxisd delta_rotation(stop_prime.rotation());
    //delta_rotation.fromRotationMatrix(stop_prime.rotation())


    // Calculate number of steps
    unsigned steps_translation = static_cast<unsigned>(delta_translation.norm() / ds) + 1;
    unsigned steps_rotation = static_cast<unsigned>(delta_rotation.angle() / dt) + 1;
    unsigned steps = std::max(steps_translation, steps_rotation);

    // Step size
    Eigen::Vector3d step = delta_translation / steps;

    // Orientation interpolation
    Eigen::Quaterniond start_q (start.rotation());
    Eigen::Quaterniond stop_q (stop.rotation());
    double slerp_ratio = 1.0 / steps;

    std::vector<Eigen::Affine3d, Eigen::aligned_allocator<Eigen::Affine3d> > result;
    Eigen::Vector3d trans;
    Eigen::Quaterniond q;
    Eigen::Affine3d pose;
    result.reserve(steps+1);
    for (unsigned i = 0; i <= steps; ++i)
    {
      trans = start_pos + step * i;
      q = start_q.slerp(slerp_ratio * i, stop_q);
      pose = (Eigen::Translation3d(trans) * q);
      result.push_back(pose);
    }
    return result;
  }
} //namespace constrained_ik

