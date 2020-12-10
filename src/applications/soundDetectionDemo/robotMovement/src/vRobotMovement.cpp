//
// Created by Daniel Gutierrez-Galan
// dgutierrez@atc.us.es
// University of Seville
// 10/Dec/2020
//

#include <yarp/os/all.h>
#include <yarp/math/Math.h>
#include <yarp/sig/Vector.h>
#include <yarp/dev/Drivers.h>
#include <yarp/dev/PolyDriver.h>
#include <yarp/dev/IControlLimits2.h>
#include <yarp/dev/IEncoders.h>
#include <yarp/dev/IControlMode.h>
#include <yarp/dev/IPositionControl.h>
#include <event-driven/all.h>

using namespace ev;
using namespace yarp::os;
using namespace yarp::dev;
using namespace yarp::sig;
using namespace yarp::math;

class vRobotMovement : public RFModule, public Thread {

private:
    // Input port: type AE
    vReadPort< vector< AE > > input_port;
    // Output port: type Bottle
    BufferedPort<Bottle> output_port;

    // ----------------------------------------------------------------------------
    //                              JOINT CONTROL
    // Joint driver
    PolyDriver joint_driver;

    // Limits of the joint
    IControllimits *joint_limits;

    // Enconder of the joint
    IEncoders *joint_enconder;

    // Control mode of the joint
    IControlMode *joint_control_mode;

    // Position control of the joint
    IPositionControl *joint_position_control;
    // ----------------------------------------------------------------------------

    // Debug flag
    // If activated, the module shows info about the classification in the terminal
    bool is_debug_flag;

    // Simulation flag
    // If activated, it means we are using the simulation platform
    bool is_simulation;

    // Number of sound source positions. This number must be the same as the number 
    // of output neurons used in vAuditoryAttention sound sources population
    int number_sound_source_neurons;

    // Body part
    std::string body_part;

    // Joint ID
    int joint_id;

    // Joint speed
    double joint_speed;

    // Joint max limit
    double joint_max_limit;

    // Joint min limit
    double joint_min_limit;

    // Joint initial position
    double joint_initial_position;

    // Joint current position
    double joint_current_position;

    // Joint desired position
    double joint_desired_position;

    // Period
    double update_period;

public:

    vRobotMovement() {}

    virtual bool configure(yarp::os::ResourceFinder& rf)
    {
        yInfo() << "Configuring the module...";

        // Set the module name used to name ports
        setName((rf.check("name", Value("/vRobotMovement")).asString()).c_str());

        // Open input port
        yInfo() << "Opening input port...";
        if(!input_port.open(getName() + "/AE:i")) {
            yError() << "Could not open input port";
            return false;
        }
        // Open output port
        yInfo() << "Opening output port...";
        if(!output_port.open(getName() + "/pos:o")) {
            yError() << "Could not open output port";
            return false;
        }

        // Read flags and parameters
        
        // Debug flag: flase by default
        is_debug_flag = rf.check("is_debug_flag") &&
                rf.check("is_debug_flag", Value(true)).asBool();
        yInfo() << "Flag is_debug_flat is: " << is_debug_flag;

        // Simulation flag: true by default
        bool default_is_simulationr = true;
        is_simulation = rf.check("is_simulation") &&
                rf.check("is_simulation", Value(default_is_simulationr)).asBool();
        yInfo() << "Flag is_simulation is: " << is_simulation;

        // Number of sound source positions to classify: 9 by default
        int default_number_sound_source_neurons = 9;
        number_sound_source_neurons = rf.check("number_sound_source_neurons",
                                    Value(default_number_sound_source_neurons)).asInt();
        yInfo() << "Setting number_sound_source_neurons parameter to: " << number_sound_source_neurons;

        // iCub body part to be moved: "head" by default
        std::string default_body_part = "head";
        body_part = rf.check("body_part",
                                    Value(default_body_part)).asString();
        yInfo() << "Setting body_part parameter to: " << body_part;

        // Joint ID to indicate the joint to be controlled: 2 by default
        int default_joint_id = 2;
        joint_id = rf.check("joint_id",
                                    Value(default_joint_id)).asInt();
        yInfo() << "Setting joint_id parameter to: " << joint_id;

        // Joint speed of movement: 10 by default
        double default_joint_speed = 10.0;
        joint_speed = rf.check("joint_speed",
                                    Value(default_joint_speed)).asDouble();
        yInfo() << "Setting joint_speed parameter to: " << joint_speed;

        // Period value: 1 sec by default
        double default_update_period = 1.0;
        update_period = rf.check("update_period",
                                    Value(default_update_period)).asDouble();
        yInfo() << "Setting update_period parameter to: " << update_period;

        // Do any other set-up required here

        // ----------------------------------------------------------------------------
        //                              JOINT CONFIGURATION
        // Open the communication with encoders
        // NOTE: check that you are able to open the iCub simulator
        Property jointEncoderProperty;

        jointEncoderProperty.put("device", "remote_controlboard");
        jointEncoderProperty.put("local", "/encReader/" + getName() + "/" + body_part);

        // If it's for simulation
        if(is_simulation == true) {
            jointEncoderProperty.put("remote", "/icubSim/" + body_part);
            // Try to open the body part
            if(!joint_driver.open(jointEncoderProperty)) {
                // If not, ERROR
                yError() << "ERROR! Unable to connect to /icubSim/" << body_part;
                return false;
            }
        } else {
            // If it's not for simulation i.e. it's for real iCub
            jointEncoderProperty.put("remote", "/icub/" + body_part);
            // Try to open the body part
            if (!joint_driver.open(jointEncoderProperty)) {
                yError() << "ERROR! Unable to connect to /icub/" << body_part;
                return false;
            }
        }

        // Try to open the views
        bool open_views_no_errors = true;

        open_views_no_errors = open_views_no_errors && joint_driver.view(joint_limits);
        open_views_no_errors = open_views_no_errors && joint_driver.view(joint_enconder);
        open_views_no_errors = open_views_no_errors && joint_driver.view(joint_control_mode);
        open_views_no_errors = open_views_no_errors && joint_driver.view(joint_position_control);

        // Check if there was any errors opening the views
        if(open_views_no_errors == false) {
            yError() << "ERROR! Unable to open views";
            return false;
        }

        // Compute joint limits
        joint_limits->getLimits(joint_id, &joint_min_limit, &joint_max_limit);
        // Show the joint limits
        yWarning() << "Joint min limit: " << joint_min_limit << " - joint max limit: " << joint_max_limit;

        // Set the joint's control mode
        joint_control_mode->setControlMode(joint_id, VOCAB_CM_POSITION);

        // Move joint to center
        // Calculate the center position
        joint_initial_position = (joint_max_limit + joint_min_limit) / 2.0;
        // Set the joint speed: for safety first move, set the speed to 10
        joint_position_control->setRefSpeed(joint_id, 10);
        // Set the joint position
        joint_position_control->positionMove(joint_id, joint_initial_position);
        // Show a message while the joint is moving
        yInfo() << "Waiting 3 seconds to reach joint_initial_position...";
        // Wait for 3 seconds
        Time::delay(3);

        // Check the speed is not too high
        if(joint_speed > 30) {
            yWarning() << "The joint speed is too high! The new value is 30!";
            joint_position_control->setRefSpeed(joint_id, 30);
        } else if(joint_speed <= 0) {
            yWarning() << "The joint speed is too low! The new value is 10!";
            joint_position_control->setRefSpeed(joint_id, 10);
        } else {
            joint_position_control->setRefSpeed(joint_id, joint_speed);
        }

        // Start the asynchronous and synchronous threads
        yInfo() << "Starting the thread...";
        return Thread::start();
    }

    virtual double getPeriod()
    {
        // Period of synchrnous thread (in seconds)
        return update_period;
    }

    bool interruptModule()
    {
        // If the module is asked to stop ask the asynchrnous thread to stop
        yInfo() << "Interrupting the module: stopping thread...";
        return Thread::stop();
    }

    void onStop()
    {
        // When the asynchrnous thread is asked to stop, close ports and do
        // other clean up
        yInfo() << "Stopping the module...";
        yInfo() << "Closing input port...";
        input_port.close();
        yInfo() << "Closing output port...";
        output_port.close();
        yInfo() << "Module has been closed!";
    }

    // Synchronous thread
    virtual bool updateModule()
    {
        // Add any synchronous operations here, visualisation, debug out prints
        double position_to_move = 0.0;

        double joint_range = joint_max_limit - joint_min_limit;

        double slot = joint_range / number_sound_source_neurons;

        position_to_move = (joint_desired_position * slot) + (slot / 2.0);

        joint_position_control->positionMove(joint_id, position_to_move);

        // Do any other set-up required here

        return Thread::isRunning();
    }

    // Asynchronous thread run forever
    void run()
    {
        // YARP timestamp
        Stamp yarpstamp;

        // Forever...
        while(true) {

            const vector<AE> * q = input_port.read(yarpstamp);
            if(!q || Thread::isStopping()) return;

            // Do asynchronous processing here
            for(auto &qi : *q) {
                
                
                joint_desired_position = qi._coded_data;

            }

        }
    }
};

int main(int argc, char * argv[])
{
    // Initialize yarp network
    yarp::os::Network yarp;
    if(!yarp.checkNetwork(2)) {
        std::cout << "Could not connect to YARP" << std::endl;
        return false;
    }

    // Prepare and configure the resource finder
    yarp::os::ResourceFinder rf;
    rf.setVerbose( false );
    rf.setDefaultContext( "eventdriven" );
    rf.setDefaultConfigFile( "vRobotMovement.ini" );
    rf.configure( argc, argv );

    // Create the module
    vRobotMovement instance;
    return instance.runModule(rf);
}