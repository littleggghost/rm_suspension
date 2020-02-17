//
// Created by qiayuan on 2/14/20.
//

#include "Simulation.h"
/*!
 * Initialize the simulator here.  It is _not_ okay to block here waiting for
 * the robot to connect. Use firstRun() instead!
 */
Simulation::Simulation(ChassisType type)
    : tau_(8) {
  // init ROS
  printf("[Simulation] Init ROS...\n");
  markerPub_ = nh_.advertise<visualization_msgs::Marker>("visualization_marker", 10);
  marker_.header.frame_id = "/map";
  marker_.header.stamp = ros::Time::now();
  marker_.ns = "basic_shapes";
  while (markerPub_.getNumSubscribers() < 1 && ros::ok()) {
    ROS_INFO_ONCE("[Simulation] Wait for subscriber to the marker\n");
    sleep(1);
  }
  twistPub_ = nh_.advertise<geometry_msgs::Twist>("base_twist", 10);

  // init parameters
  printf("[Simulation] Load parameters...\n");
  simParams_.getParam(&nh_);
  // init chassis info
  printf("[Simulation] Build chassis...\n");
  type_ = type;
  chassis_ = buildStandardChassis<double>();
  printf("[Simulation] Build actuator model...\n");
  actuatorModels_ = chassis_.buildActuatorModels();

  // init rigid body dynamics
  printf("[Simulation] Build rigid body model...\n");

  model_ = chassis_.buildModel();
  simulator_ =
      new DynamicsSimulator<double>(model_, simParams_.use_spring_damper_);

  DVec<double> zero8(8);
  for (u32 i = 0; i < 8; i++) {
    zero8[i] = 0.;
  }

  // set some sane defaults:
  tau_ = zero8;
  jointState_.q = zero8;
  jointState_.qd = zero8;
  FBModelState<double> x0;
  x0.bodyOrientation = rotationMatrixToQuaternion(
      ori::coordinateRotation(CoordinateAxis::Z, 0.0));
  x0.bodyPosition.setZero();
  x0.bodyVelocity.setZero();
  x0.q = zero8;
  x0.qd = zero8;

  x0.bodyPosition[2] = 1.2;
  setRobotState(x0);
  addCollisionPlane(0.7, 0, 0.);
  Vec3<double> pos(0, 0, 0.1);
  addCollisionBox(0.7, 0, 4., 1, 0.2, pos,
                  coordinateRotation<double>(CoordinateAxis::Z, 0));
  printf("[Simulation] Ready!\n");
}

void Simulation::step(double dt, double dtControl) {
  //Fake suspension
  for (int wheel = 0; wheel < 4; ++wheel) {
    suspe_data_.q_[wheel] = simulator_->getState().q[wheel * 2];
    suspe_data_.qd_[wheel] = simulator_->getState().qd[wheel * 2];
  }
  fake_suspe_.update(suspe_data_);
  for (int wheel = 0; wheel < 4; ++wheel) {
    tau_[wheel * 2] = fake_suspe_.torque_out_[wheel];
  }

  // Low level control
  if (currentSimTime_ >= timeOfNextLControl_) {
    double qd[4] = {simulator_->getState().qd[1],
                    simulator_->getState().qd[3],
                    simulator_->getState().qd[5],
                    simulator_->getState().qd[7]};
    controller_.update(qd, currentSimTime_);
    timeOfNextLControl_ = timeOfNextLControl_ + dtControl;
  }
  // actuator model:
  for (int wheelID = 0; wheelID < 4; wheelID++) {
    tau_[wheelID * 2 + 1] = actuatorModels_[0].getTorque(
        controller_.torque_out_[wheelID],
        simulator_->getState().qd[wheelID * 2 + 1]);
  }
  // dynamics
  currentSimTime_ += dt;

  // Set Homing Information
  RobotHomingInfo<double> homing;
  homing.active_flag = simParams_.go_home_;
  homing.position = simParams_.home_pos_;
  homing.rpy = simParams_.home_rpy_;
  homing.kp_lin = simParams_.home_kp_lin_;
  homing.kd_lin = simParams_.home_kd_lin_;
  homing.kp_ang = simParams_.home_kp_ang_;
  homing.kd_ang = simParams_.home_kd_ang_;
  simulator_->setHoming(homing);
  simulator_->step(dt, tau_, simParams_.floor_kp_, simParams_.floor_kd_);
}

/*!
 * Add an infinite collision plane to the simulator
 * @param mu          : friction of the plane
 * @param resti       : restitution coefficient
 * @param height      : height of plane
 * @param addToWindow : if true, also adds graphics for the plane
 */
void Simulation::addCollisionPlane(double mu, double resti, double height,
                                   double sizeX, double sizeY) {
  simulator_->addCollisionPlane(mu, resti, height);
  marker_.id = 0;
  marker_.type = visualization_msgs::Marker::CUBE;
  marker_.action = visualization_msgs::Marker::ADD;

  marker_.pose.position.z = -0.001;
  marker_.pose.orientation.x = 0.0;
  marker_.pose.orientation.y = 0.0;
  marker_.pose.orientation.z = 0.0;
  marker_.pose.orientation.w = 1.0;
  // Set the scale of the marker -- 1x1x1 here means 1m on a side
  marker_.scale.x = sizeX;
  marker_.scale.y = sizeY;
  marker_.scale.z = 0.001;
  // Set the color -- be sure to set alpha to something non-zero!
  marker_.color.r = 1.0f;
  marker_.color.g = 1.0f;
  marker_.color.b = 1.0f;
  marker_.color.a = 0.5;
  marker_.lifetime = ros::Duration(0);
  markerPub_.publish(marker_);
}

/*!
 * Add an box collision to the simulator
 * @param mu          : location of the box
 * @param resti       : restitution coefficient
 * @param depth       : depth (x) of box
 * @param width       : width (y) of box
 * @param height      : height (z) of box
 * @param pos         : position of box
 * @param ori         : orientation of box
 * @param addToWindow : if true, also adds graphics for the plane
 */
void Simulation::addCollisionBox(double mu, double resti, double depth,
                                 double width, double height,
                                 const Vec3<double> &pos,
                                 const Mat3<double> &ori) {
  simulator_->addCollisionBox(mu, resti, depth, width, height, pos, ori);
  marker_.id = 1;
  marker_.type = visualization_msgs::Marker::CUBE;
  marker_.action = visualization_msgs::Marker::ADD;

  marker_.pose.position.z = pos.z();
//  marker_.pose.orientation.x = ;
//  marker_.pose.orientation.y = ;
//  marker_.pose.orientation.z = ;
//  marker_.pose.orientation.w = ;

  // Set the scale of the marker -- 1x1x1 here means 1m on a side
  marker_.scale.x = depth;
  marker_.scale.y = width;
  marker_.scale.z = height;
  // Set the color -- be sure to set alpha to something non-zero!
  marker_.color.r = 1.0f;
  marker_.color.g = 1.0f;
  marker_.color.b = 1.0f;
  marker_.color.a = 0.5;
  marker_.lifetime = ros::Duration(0);
  markerPub_.publish(marker_);
}

void Simulation::addCollisionMesh(double mu, double resti, double grid_size,
                                  const Vec3<double> &left_corner_loc,
                                  const DMat<double> &height_map,
                                  bool addToWindow, bool transparent) {
  simulator_->addCollisionMesh(mu, resti, grid_size, left_corner_loc,
                               height_map);
}

/*!
 * Runs the simulator for time xxx
  * @param
 */
void Simulation::runForTime(double time) {
  printf("[Simulation] Computing...\n");
  visData_.clear();
  controller_.setSpeed(3. / chassis_._wheelRadius);
  while (currentSimTime_ < time && ros::ok()) {
    step(simParams_.dynamics_dt_, simParams_.control_dt_);
    if (currentSimTime_ >= timeOfRecord_) {
      record();
      timeOfRecord_ = currentSimTime_ + 1. / simParams_.vis_fps_;
    }
    if (ros::Time::now().toSec() >= timeOfPrint_) {
      printf("\r[Simulation] %.5lf%%", currentSimTime_ / time * 100.);
      fflush(stdout);
      timeOfPrint_ = ros::Time::now().toSec() + 1.;
    }
  }
}

void Simulation::record() {
  VisData vis_data;
  /////////////////////////////////record TF Data///////////////////////////////////
  vis_data.tfPos[0] = simulator_->getState().bodyPosition;
  vis_data.tfQuat[0] = simulator_->getState().bodyOrientation;
  //Note!!!!!!You can't use getOrientation(5) to get the orientation of base
  for (int wheelID = 0; wheelID < 4; ++wheelID) {
    vis_data.tfQuat[wheelID * 2 + 1] = rotationMatrixToQuaternion(model_.getOrientation(wheelID * 2 + 6));
    vis_data.tfPos[wheelID * 2 + 1] = model_.getPosition(wheelID * 2 + 6);
    vis_data.tfQuat[wheelID * 2 + 2] = rotationMatrixToQuaternion(model_.getOrientation(wheelID * 2 + 7));
    vis_data.tfPos[wheelID * 2 + 2] = model_.getPosition(wheelID * 2 + 7);
  }
  //////////////////////////record contact force data///////////////////////////////
  int _nTotalGC = simulator_->getTotalNumGC();

  for (size_t i(0); i < _nTotalGC; ++i) {
    Vec3<double> f = simulator_->getContactForce(i);
    if (f.norm() > 0.) {
      vis_data.cpForce.push_back(f);
      vis_data.cpPos.push_back(simulator_->getModel()._pGC[i]);
    }
  }
  ///////////////////////////////record base Twist for plot//////////////////////////
  vis_data.baseMsg.linear.x = vis_data.tfPos[0].x();
  vis_data.baseMsg.linear.y = vis_data.tfPos[0].y();
  vis_data.baseMsg.linear.z = vis_data.tfPos[0].z();
  Vec3<double> rpy = quatToRPY(simulator_->getState().bodyOrientation);
  vis_data.baseMsg.angular.x = rpy.x();
  vis_data.baseMsg.angular.y = rpy.y();
  vis_data.baseMsg.angular.z = rpy.z();

  visData_.push_back(vis_data);
}

void Simulation::play(double scale) {
  printf("\n[Simulation] Start play!\n");
  auto iter = visData_.begin();
  ros::Rate loop_rate(simParams_.vis_fps_ / scale);
  while (ros::ok() && iter != visData_.end()) {
    sendTf(iter);
    sendCP(iter);
    sendMsg(iter);
    ++iter;
    loop_rate.sleep();
  }
}

void Simulation::sendTf(vector<VisData>::iterator iter) {
  tf::Quaternion quat_tf;
  tf::Transform tf;
  std::string frame;

  ros::Time now = ros::Time::now();
  quat_tf.setValue(iter->tfQuat[0][1],
                   iter->tfQuat[0][2],
                   iter->tfQuat[0][3],
                   iter->tfQuat[0][0]);
  tf.setRotation(quat_tf);
  tf.setOrigin(tf::Vector3(iter->tfPos[0].x(), iter->tfPos[0].y(), iter->tfPos[0].z()));
  br_.sendTransform(tf::StampedTransform(tf, now, "map", "base_link"));
  for (int wheelID = 0; wheelID < 4; ++wheelID) {
    quat_tf.setValue(iter->tfQuat[wheelID * 2 + 1][1],
                     iter->tfQuat[wheelID * 2 + 1][2],
                     iter->tfQuat[wheelID * 2 + 1][3],
                     iter->tfQuat[wheelID * 2 + 1][0]);
    tf.setRotation(quat_tf);
    tf.setOrigin(tf::Vector3(iter->tfPos[wheelID * 2 + 1].x(),
                             iter->tfPos[wheelID * 2 + 1].y(),
                             iter->tfPos[wheelID * 2 + 1].z()));
    frame = "suspension_";
    frame += std::to_string(wheelID);
    br_.sendTransform(tf::StampedTransform(tf, now, "map", frame));

    quat_tf.setValue(iter->tfQuat[wheelID * 2 + 2][1],
                     iter->tfQuat[wheelID * 2 + 2][2],
                     iter->tfQuat[wheelID * 2 + 2][3],
                     iter->tfQuat[wheelID * 2 + 2][0]);
    tf.setRotation(quat_tf);
    tf.setOrigin(tf::Vector3(iter->tfPos[wheelID * 2 + 2].x(),
                             iter->tfPos[wheelID * 2 + 2].y(),
                             iter->tfPos[wheelID * 2 + 2].z()));
    frame = "wheel_";
    frame += std::to_string(wheelID);
    br_.sendTransform(tf::StampedTransform(tf, now, "map", frame));
  }
}

void Simulation::sendCP(vector<VisData>::iterator iter) {
  visualization_msgs::Marker marker;
  marker.header.frame_id = "map";
  marker.header.stamp = ros::Time::now();
  marker.ns = "my_namespace";
  marker.color.a = 1.0; // Don't forget to set the alpha!
  marker.color.g = 1.0;
  marker.scale.x = .01;
  marker.pose.orientation.w = 1;
  marker.id = 2;
  marker.type = visualization_msgs::Marker::LINE_LIST;
  marker.action = visualization_msgs::Marker::ADD;
  marker.lifetime = ros::Duration(0.05);

  auto cpP = iter->cpPos.begin();
  auto cpF = iter->cpForce.begin();

  while (cpP != iter->cpPos.end()) {
    geometry_msgs::Point p;
    p.x = cpP->x();
    p.y = cpP->y();
    p.z = cpP->z();
    marker.points.push_back(p);
    p.x = cpP->x() + 0.005 * cpF->x();
    p.y = cpP->y() + 0.005 * cpF->y();
    p.z = cpP->z() + 0.005 * cpF->z();
    marker.points.push_back(p);
    markerPub_.publish(marker);
    cpP++;
    cpF++;
    marker.points.clear();
    marker.id++;
  }
}

void Simulation::sendMsg(vector<VisData>::iterator iter) {
  twistPub_.publish(iter->baseMsg);
}

