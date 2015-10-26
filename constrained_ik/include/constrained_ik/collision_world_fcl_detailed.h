#ifndef COLLISION_WORLD_FCL_DETAILED_H
#define COLLISION_WORLD_FCL_DETAILED_H
#include <moveit/collision_detection_fcl/collision_world_fcl.h>
#include <eigen3/Eigen/Core>
#include <moveit/robot_model/robot_model.h>
#include <moveit/robot_state/robot_state.h>
#include <constrained_ik/collision_common_detailed.h>

namespace constrained_ik
{
  class CollisionWorldFCLDetailed : public collision_detection::CollisionWorldFCL
  {
  public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW

    typedef boost::shared_ptr<CollisionWorldFCLDetailed> CollisionWorldFCLDetailedPtr;

    CollisionWorldFCLDetailed(): CollisionWorldFCL() {}
    explicit CollisionWorldFCLDetailed(const collision_detection::WorldPtr& world): CollisionWorldFCL(world) {}
    CollisionWorldFCLDetailed(const CollisionWorldFCL &other, const collision_detection::WorldPtr& world): CollisionWorldFCL(other, world) {}
    virtual ~CollisionWorldFCLDetailed() {}

    virtual void distanceRobot(const DistanceRequest &req, DistanceResult &res, const collision_detection::CollisionRobot &robot, const robot_state::RobotState &state) const;

    virtual void distanceRobotDetailedHelper(const DistanceRequest &req, DistanceResult &res, const collision_detection::CollisionRobot &robot, const robot_state::RobotState &state) const;
  };
}

#endif // COLLISION_WORLD_FCL_DETAILED_H

