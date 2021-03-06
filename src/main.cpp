#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "spline.h"

using namespace std;

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, vector<double> maps_x, vector<double> maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, vector<double> maps_x, vector<double> maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2( (map_y-y),(map_x-x) );

	double angle = abs(theta-heading);

	if(angle > pi()/4)
	{
		closestWaypoint++;
	}

	return closestWaypoint;

}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, vector<double> maps_x, vector<double> maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, vector<double> maps_s, vector<double> maps_x, vector<double> maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}

vector<double> shift_point(double px, double py, double yaw, double origin_x, double origin_y){

    double diff_x = px-origin_x;
    double diff_y = py-origin_y;

    double shift_x = (diff_x *cos(0-yaw)-diff_y*sin(0-yaw));
    double shift_y = (diff_x *sin(0-yaw)+diff_y*cos(0-yaw));
    return {shift_x, shift_y};
}

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;
  double ref_v = 49.5;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }

  double ref_inc = 0.430;
  double dist_inc = 0;
  double target_lane = 1;
  h.onMessage([&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy, &ref_inc, &dist_inc, &target_lane](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
        	// Main car's localization Data
          	double car_x = j[1]["x"];
          	double car_y = j[1]["y"];
          	double car_s = j[1]["s"];
          	double car_d = j[1]["d"];
          	double car_yaw = j[1]["yaw"];
          	double car_speed = j[1]["speed"];

          	// Previous path data given to the Planner
          	auto previous_path_x = j[1]["previous_path_x"];
          	auto previous_path_y = j[1]["previous_path_y"];
          	// Previous path's end s and d values 
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
          	auto sensor_fusion = j[1]["sensor_fusion"];

          	json msgJson;

          	vector<double> next_x_vals;
            vector<double> next_y_vals;


            int size = previous_path_x.size();
            for (int j = 0; j< size; j++){
                next_x_vals.push_back(previous_path_x[j]);
                next_y_vals.push_back(previous_path_y[j]);
            }

            tk::spline spline;
            vector<double> spline_points_x;
            vector<double> spline_points_y;

            //keep lane
            ref_inc = 0.430;
            int lane = 1;
            double s, d, latest_yaw;
            double last_x, last_y;
            double car_yaw_rad = deg2rad(car_yaw);
            if (size == 0){
                s = car_s;
                d = car_d;
                last_x = car_x;
                last_y = car_y;
                double bef_last_x = car_x - cos(car_yaw);
                double bef_last_y = car_y - sin(car_yaw);
                latest_yaw = car_yaw_rad;
            }
//            else if(size == 1){
//                s = end_path_s;
//                d = end_path_d;
//                last_x = previous_path_x.back();
//                last_y = previous_path_y.back();
//                double bef_last_x = car_x - cos(car_yaw);
//                double bef_last_y = car_y - sin(car_yaw);
//                latest_yaw = atan2(car_x-bef_last_x, car_y-bef_last_y);
//                spline_points_x.push_back(bef_last_x);
//                spline_points_y.push_back(bef_last_y);
//            }
            else{
                s = end_path_s;
                d = end_path_d;
                last_x = previous_path_x.back();
                last_y = previous_path_y.back();
                double bef_last_x = previous_path_x[size-2];
                double bef_last_y = previous_path_y[size-2];
                latest_yaw = atan2(last_y-bef_last_y, last_x-bef_last_x);
                vector<double> shifted_car_pos = shift_point(bef_last_x, bef_last_y, latest_yaw, last_x, last_y);
                spline_points_x.push_back(shifted_car_pos[0]);
                spline_points_y.push_back(shifted_car_pos[1]);
            }
            vector<double> shifted_last = shift_point(last_x, last_y, latest_yaw, last_x, last_y);
            spline_points_x.push_back(shifted_last[0]);
            spline_points_y.push_back(shifted_last[1]);


            // Decide what to do next
            double vx, vy, vs, vd, v, v_mps, dist_diff;
            int my_current_lane = (int)(d/4);
            int v_lane;
            bool car_in_front = false;
            bool try_to_switch = false;
            double min_dist = 99999.0;
            double current_ddiff;
            int spline_inc = 25;
            bool is_car_left = false, is_car_right = false;
            if(my_current_lane == 0){
                cout << "CAR LEFT!!!" << endl;
                is_car_left = true; //we dont switch left
            }else if(my_current_lane == 2){
                cout << "CAR RIGHT!!!" << endl;
                is_car_right = true; //we dont switch right
            }

            cout << "My car: d: " << car_d << ", lane: " << my_current_lane << ", s: " << s << endl;
            for(int i = 0; i < sensor_fusion.size(); i++){
                vx = sensor_fusion[i][3];
                vy = sensor_fusion[i][4];
                vs = sensor_fusion[i][5];
                vd = sensor_fusion[i][6];
                v  = sqrt(vx*vx+vy*vy);
                v_mps = (v * 1.6 / 36)*0.2;
                v_lane = (int)(vd/4);
                dist_diff = vs-s;
                current_ddiff = vs - car_s;
                cout << "Car: " << i << ", vd: " << vd << ", v_lane: " << v_lane << ", vs: " << vs << ", dist_diff: " << dist_diff << endl;
                if(my_current_lane == v_lane){

                    if((dist_diff < 5) && (dist_diff > 0)){
                        //my speed = car_speed
                        ref_inc = v_mps;
                        cout << "SWITCH!!!" << endl;
                        try_to_switch = true;
//                        cout << "Dist diff: " << dist_diff << endl;
                    }else{
//                        cout << "Dist diff: " << dist_diff << endl;
                        //Do nothing special for now

                    }
                }
                else if(!is_car_left && (v_lane == (my_current_lane-1))){
//                    if((dist_diff < 25) && (dist_diff >-5) && (current_ddiff > -3)){
                    if((vs > (car_s-5)) && (vs < s+10)){
                           is_car_left = true;
                    }
                }
                else if(!is_car_right && (v_lane == (my_current_lane+1))){
//                    if((dist_diff < 25) && (dist_diff >- 5) && (current_ddiff > -3)){
                    if((vs > (car_s-5)) && (vs < (s+10))){
                           is_car_right = true;
                    }
                }
                if((current_ddiff < 10) && (current_ddiff > -3) && abs(vd - car_d) < 1){
                    // my speed slows down
                    target_lane = my_current_lane;
                    ref_inc = v_mps-v_mps;
                    try_to_switch = false;
                    break;
                }



            }


            if (try_to_switch){
                if(!is_car_left){
                    target_lane = my_current_lane-1;
                    ref_inc = 0.430;
                    spline_inc = 100;
                }
                else if(!is_car_right){
                    target_lane = my_current_lane+1;
                    ref_inc = 0.430;
                    spline_inc = 100;
                }
            }
            cout << "Target lane: " << target_lane << endl;
            cout << "current lane: " << my_current_lane << endl;
            int n = 3;
            double shift = (target_lane-my_current_lane);
            for (int i = 1; i <= n; ++i){
//                double next_d = 2+4*(my_current_lane+shift);
                double next_d = 2+4*target_lane;
                if(target_lane!=my_current_lane){
                    next_d = 3+3*target_lane;
                }
                cout << "next-d: " << next_d << endl;
                vector<double> p = getXY(s+spline_inc*i,next_d,map_waypoints_s,map_waypoints_x,map_waypoints_y);

                vector<double> p_shifted =  shift_point(p[0], p[1], latest_yaw, last_x, last_y);

                spline_points_x.push_back(p_shifted[0]);
                spline_points_y.push_back(p_shifted[1]);

            }

//            for (int i = 0; i< spline_points_x.size(); ++i){
//                cout << "x: " << spline_points_x[i] << ", y: " << spline_points_y[i] << endl;
//            }

            spline.set_points(spline_points_x, spline_points_y);

            // Perform the decision
//            cout << "Car speed: " << car_speed << endl;
//            cout << "Last yaw: " << latest_yaw << endl;
            double temp_way_point_x = 0, temp_way_point_y = 0;
            for (int i=0; i <= (50-size); ++i){
//                cout << "dist_inc: " << dist_inc << endl;
                if(dist_inc < ref_inc){
                    dist_inc += 0.002;
                }else{
                    dist_inc -= 0.002;
                }
                temp_way_point_x += dist_inc; //shift coordinates would be better I guess.
                temp_way_point_y = spline(temp_way_point_x);

//                cout << "x back: " << temp_way_point_x << ", y back: " << temp_way_point_y << endl;

                double shift_back_x = (temp_way_point_x *cos(latest_yaw)-temp_way_point_y*sin(latest_yaw)) + last_x;
                double shift_back_y = (temp_way_point_x *sin(latest_yaw)+temp_way_point_y*cos(latest_yaw)) + last_y;

                next_x_vals.push_back(shift_back_x);
                next_y_vals.push_back(shift_back_y);
//                cout << "X: " << next_x_vals[i] << endl;
//                cout << "Y: " << next_y_vals[i] << endl;
            }

          	msgJson["next_x"] = next_x_vals;
          	msgJson["next_y"] = next_y_vals;

          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
















































































