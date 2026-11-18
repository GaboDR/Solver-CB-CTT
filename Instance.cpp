#include "Instance.hpp"

static std::string trimColon(const std::string& s) {
    std::string out = s;
    out.erase(std::remove(out.begin(), out.end(), ':'), out.end());
    return out;
}

bool Instance::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: No se pudo abrir el archivo de instancia " << filename << "\n";
        return false;
    }

    std::string token;
    // 1. Lectura de Cabecera
    while (file >> token) {
        std::string key = trimColon(token);
        if (key == "Name") file >> name;
        else if (key == "Courses") file >> num_courses;
        else if (key == "Rooms") file >> num_rooms;
        else if (key == "Days") file >> days;
        else if (key == "Periods_per_day") file >> periods_per_day;
        else if (key == "Curricula") file >> num_curricula;
        else if (key == "Constraints") {
            file >> num_constraints;
            break;
        }
    }

    total_periods = days * periods_per_day;
    courses.resize(num_courses);
    rooms.resize(num_rooms);
    curricula.resize(num_curricula);
    unavailable.assign(num_courses, std::vector<bool>(total_periods, false));
    conflict.assign(num_courses, std::vector<bool>(num_courses, false));
    in_curriculum.assign(num_curricula, std::vector<bool>(num_courses, false));

    // 2. Lectura de Secciones
    while (file >> token) {
        if (token == "COURSES:") {
            for (int i = 0; i < num_courses; ++i) {
                Course& c = courses[i];
                c.id = i;
                file >> c.code >> c.teacher >> c.lectures >> c.min_working_days >> c.students;
                course_map[c.code] = i;
                if (teacher_map.find(c.teacher) == teacher_map.end()) {
                    teacher_map[c.teacher] = num_teachers++;
                }
                c.teacher_id = teacher_map[c.teacher];
            }
        } else if (token == "ROOMS:") {
            for (int i = 0; i < num_rooms; ++i) {
                Room& r = rooms[i];
                r.id = i;
                file >> r.code >> r.capacity;
                room_map[r.code] = i;
            }
        } else if (token == "CURRICULA:") {
            for (int u = 0; u < num_curricula; ++u) {
                Curriculum& curr = curricula[u];
                curr.id = u;
                int member_count = 0;
                file >> curr.code >> member_count;
                for (int m = 0; m < member_count; ++m) {
                    std::string c_code;
                    file >> c_code;
                    int c_id = course_map.at(c_code);
                    curr.courses.push_back(c_id);
                    courses[c_id].curricula.push_back(u);
                    in_curriculum[u][c_id] = true;
                }
            }
        } else if (token == "UNAVAILABILITY_CONSTRAINTS:") {
            for (int k = 0; k < num_constraints; ++k) {
                std::string c_code;
                int d, s;
                file >> c_code >> d >> s;
                int c_id = course_map.at(c_code);
                int p = getPeriod(d, s);
                unavailable[c_id][p] = true;
            }
        } else if (token == "END.") {
            break;
        }
    }

    // 3. Precalcular matriz de conflictos (mismo profesor o mismo currículo)
    for (int c1 = 0; c1 < num_courses; ++c1) {
        conflict[c1][c1] = true; // Un curso no puede dictarse dos veces en el mismo periodo
        for (int c2 = c1 + 1; c2 < num_courses; ++c2) {
            bool same_teacher = (courses[c1].teacher_id == courses[c2].teacher_id);
            bool shared_curr = false;
            for (int u : courses[c1].curricula) {
                if (in_curriculum[u][c2]) {
                    shared_curr = true;
                    break;
                }
            }
            if (same_teacher || shared_curr) {
                conflict[c1][c2] = conflict[c2][c1] = true;
            }
        }
    }
    return true;
}

void Instance::printSummary() const {
    std::cout << "=== Instancia Cargada: " << name << " ===\n"
              << "Cursos: " << num_courses << " | Salas: " << num_rooms
              << " | Dias: " << days << " | Bloques/dia: " << periods_per_day
              << " (Total periodos: " << total_periods << ")\n"
              << "Curriculos: " << num_curricula
              << " | Indisponibilidades: " << num_constraints
              << " | Profesores unicos: " << num_teachers << "\n";
}