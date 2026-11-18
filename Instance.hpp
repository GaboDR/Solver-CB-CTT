#ifndef INSTANCE_HPP
#define INSTANCE_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

struct Course {
    int id;
    std::string code;
    std::string teacher;
    int teacher_id;
    int lectures;           //  Clases semanales requeridas
    int min_working_days;   //  Mínimo de días de clase
    int students;           //  Cantidad de estudiantes
    std::vector<int> curricula; // IDs de currículos a los que pertenece
};

struct Room {
    int id;
    std::string code;
    int capacity;           
};

struct Curriculum {
    int id;
    std::string code;
    std::vector<int> courses; // IDs internos 
};

class Instance {
public:
    std::string name;
    int num_courses = 0;
    int num_rooms = 0;
    int days = 0;
    int periods_per_day = 0;
    int total_periods = 0;
    int num_curricula = 0;
    int num_constraints = 0;
    int num_teachers = 0;

    std::vector<Course> courses;
    std::vector<Room> rooms;
    std::vector<Curriculum> curricula;

    std::unordered_map<std::string, int> course_map;
    std::unordered_map<std::string, int> room_map;
    std::unordered_map<std::string, int> teacher_map;

    // unavailable[c][p] == true si el curso c NO está disponible en el periodo p
    std::vector<std::vector<bool>> unavailable;
    // conflict[c1][c2] == true si c1 y c2 comparten profesor o algún currículo
    std::vector<std::vector<bool>> conflict;
    // in_curriculum[u][c] == true si el curso c pertenece al currículo u
    std::vector<std::vector<bool>> in_curriculum;

    bool loadFromFile(const std::string& filename);
    int getPeriod(int day, int slot) const { return day * periods_per_day + slot; }
    int getDay(int period) const { return period / periods_per_day; }
    int getSlot(int period) const { return period % periods_per_day; }
    void printSummary() const;
};

#endif // INSTANCE_HPP