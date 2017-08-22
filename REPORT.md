# CarND-Path-Planning-Project
In this project I implemented a path-planner for a self driving car.

## 1. Algorithm Description
The algorithm I used can be described as an final state machine and very abstract looks as follows:
1. For all vehicles check and remember if one is in front of ego vehicle path or next to you (at left or right side).
2. If no vehicle is in front of us, we go by 50 miles/h. speed. GOTO 1.
3. Else adjust your speed to vehicle in front (I asume it's smaler-equal than the speed limit)
4. Check if we saw recently on left or right lane a vehicle which hinder us to switch.
5. If one of the lanes is safe perform the switch. GOTO 1.

## 2. Implementation
To generate a smother lane change trajectory, I used as suggested the `spline.h` library.
The whole algorithm is implemented in `main.cpp` but for futher improvement and refactoring it would make sense to move parts of the code into different functions and files.

## Results
A video of the path planner can be viewed on [youtube](https://www.youtube.com/watch?v=JsTjAvB6agU&feature=youtu.be).

