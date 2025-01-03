#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Constrained_Delaunay_triangulation_2.h>
#include <CGAL/mark_domain_in_triangulation.h>
#include <CGAL/Delaunay_mesher_2.h>
#include "triangulation.h"
#include "strategies.h"
#include <CGAL/Polygon_2.h>
#include <CGAL/draw_triangulation_2.h>
#include <CGAL/centroid.h>
#include <CGAL/convex_hull_2.h>
#include <unordered_map>
#include <random>
#include <cmath>
#include "old_triangulation.h"


//////////////////////////////////////////////////////////
// kernel
typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
typedef CGAL::Simple_cartesian<double> Kernel;
typedef CGAL::Triangulation_vertex_base_2<K> Vb;
typedef CGAL::Constrained_triangulation_face_base_2<K> Fb;
typedef CGAL::Triangulation_data_structure_2<Vb, Fb> Tds;
typedef CGAL::Exact_predicates_tag Itag;
typedef CGAL::Constrained_Delaunay_triangulation_2<K, Tds, Itag> CDT;
typedef K::Line_2 Line;
typedef CDT::Point Point;
typedef CGAL::Polygon_2<K> Polygon_2;
typedef CDT::Face_handle Face_handle;
using namespace std;

// State definition
struct State
{
    CDT cdt;
    int obtuse_count;
    int steiner_points;
    vector<Point> steiner_locations;
    vector<int> strategies;
    //Υπερφορτωση για το ==
    bool operator==(const State &other) const
    {
        return obtuse_count == other.obtuse_count &&
               steiner_points == other.steiner_points &&
               steiner_locations == other.steiner_locations &&
               strategies == other.strategies;
    }
};
//εκτυπωνουμε τα στοιχεία της κατάστάσης
void printStateDetails(State &state)
{
    cout << "Obtuse Count: " << state.obtuse_count << endl;
    cout << "Steiner Points: " << state.steiner_points << endl;
    cout << endl;
}
// Συνάρτηση για τη σύγκριση δύο καταστάσεων State
bool compareStates(const State &a, const State &b)
{
    return a.obtuse_count == b.obtuse_count &&
           a.steiner_points == b.steiner_points &&
           a.steiner_locations == b.steiner_locations &&
           a.strategies == b.strategies;
}

struct StateHash
{
    size_t operator()(const State &s) const
    {
        size_t hash_val = 0;
        // Hash για obtuse_count, steiner_points, και άλλα χαρακτηριστικά
        hash_val ^= hash<int>{}(s.obtuse_count) + 0x9e3779b9 + (hash_val << 6) + (hash_val >> 2);
        hash_val ^= hash<int>{}(s.steiner_points) + 0x9e3779b9 + (hash_val << 6) + (hash_val >> 2);
        for (const auto &loc : s.steiner_locations)
        {
            hash_val ^= hash<double>{}(loc.x()) + 0x9e3779b9 + (hash_val << 6) + (hash_val >> 2);
            hash_val ^= hash<double>{}(loc.y()) + 0x9e3779b9 + (hash_val << 6) + (hash_val >> 2);
        }
        return hash_val;
    }
};
//ελενχος αν τα τριγωνα ειναι αμβλειγωνια
bool is_obtuse_triangle(CDT::Face_handle face) {
    Point a = face->vertex(0)->point();
    Point b = face->vertex(1)->point();
    Point c = face->vertex(2)->point();
    return is_obtuse_angle(a, b, c) || is_obtuse_angle(b, c, a) || is_obtuse_angle(c, a, b);
}

State local_triangulation(CDT &initial_cdt, Polygon_2 &convex_hull, int &best_obtuse, CDT &best_cdt, int max_iterations)
{
    queue<State> queue;
    unordered_set<State, StateHash> visited; // Χρησιμοποιούμε custom hash για State
    // Αρχικοποίηση με την αρχική κατάσταση
    State initial_state = {initial_cdt, count_Obtuse_Angles(initial_cdt), 0, {}, {}};
    State best_state = initial_state;
    queue.push(initial_state);
    visited.insert(initial_state);
    int iteration_count = 0;
    best_cdt = initial_cdt;

    // εξερεύνηση μεσω local
    while (!queue.empty() && iteration_count < max_iterations && best_state.obtuse_count > 0)
    {
        State current_state = queue.front();
        queue.pop();
        // αν η τρεχουσα κατάσταση ειναι βελτιστη, ενημερωνουμε τη βελτιστη λυση
        if (current_state.obtuse_count < best_state.obtuse_count)
        {
            best_cdt = current_state.cdt;
            best_state = current_state;
            iteration_count -= 10; // Επαναφορά του μετρητή επαναλήψεων επειδή βελτιώθηκε
        }
        // Αν φτάσουμε στο μέγιστο βάθος ή δεν έχουμε άλλες αμβλείες γωνίες, σταματάμε
        if (iteration_count >= max_iterations || best_state.obtuse_count == 0)
            return best_state;
        // Εξερεύνηση όλων των τριγώνων με αμβλείες γωνίες
        for (auto fit = current_state.cdt.finite_faces_begin(); fit != current_state.cdt.finite_faces_end(); ++fit)
        {
            Point a = fit->vertex(0)->point();
            Point b = fit->vertex(1)->point();
            Point c = fit->vertex(2)->point();

            //  Δοκιμή όλων των στρατηγικών
            for (int strategy = 0; strategy < 5; ++strategy)
            {
                Point steiner = select_steiner_point(a, b, c, strategy, current_state.cdt, convex_hull);
                // Έλεγχος αν το σημείο είναι μέσα στο κυρτό περίβλημα
                if (convex_hull.bounded_side(steiner) == CGAL::ON_BOUNDED_SIDE || convex_hull.bounded_side(steiner) == CGAL::ON_BOUNDARY)
                {
                    CDT temp_cdt = current_state.cdt;
                    temp_cdt.insert(steiner);
                    int new_obtuse = count_Obtuse_Angles(temp_cdt);
                    // Δημιουργούμε μια νέα κατάσταση και ελέγχουμε αν υπάρχει ήδη
                    State new_state = {temp_cdt, new_obtuse, current_state.steiner_points + 1, {}, {}};
                    if (new_obtuse <= initial_state.obtuse_count)
                    {
                        // Αν η νέα κατάσταση δεν έχει επισκεφθεί ξανά, την προσθέτουμε
                        if (visited.find(new_state) == visited.end())
                        {
                            queue.push(new_state);
                            visited.insert(new_state);
                        }
                    }
                    else
                    {
                        visited.insert(new_state);
                    }
                }
            }
            //αυξηση του μετρητη αν δεν υπαρχει βελτιωση
            if (current_state.obtuse_count >= best_state.obtuse_count)
            {
                iteration_count++;
            }
        }
    }
    return best_state;
}
State sa_triangulation(CDT& cdt, const Polygon_2& convex_hull, int initial_obtuse, CDT& best_cdt, double alpha ,double beta,int L)
{
    
    //οριζουμε την ενεργεια ανάλυσης για τα states
    auto energy = [alpha, beta](const CDT& triangulation, int initial_vertices) {
        int obtuse_count = count_Obtuse_Angles(const_cast<CDT&>(triangulation));
        int steiner_count = triangulation.number_of_vertices() - initial_vertices;
        return alpha * obtuse_count + beta * steiner_count;
    };
    // αρχικοποιουμε την τρεχουσα κατασταση  CD
    State current_state;
    current_state.cdt = cdt;
    current_state.obtuse_count = initial_obtuse;
    current_state.steiner_points = 0;
    //βαζουμε το τρεχουση κατασταση σαν καλυτερη
    State best_state = current_state;
    double current_energy = energy(cdt, cdt.number_of_vertices());
    double best_energy = current_energy;
    //δημιουργια τυχαιων αριθμων
    random_device rd;
    mt19937 gen(rd());
    uniform_real_distribution<> dis(0.0, 1.0);
    //οριζουμε θερμοκρασια για την sa
    double temperature = 1.0;
//main επαναληψη της sa
while (temperature >= 0) {
    //κανουμε  L επαναληψεις για καθε θεμοκρασια
    for (int i = 0; i < L; ++i) {
        // βρισκουμε ολα τα τριγωνα με αμβλειες
        vector<CDT::Face_handle> obtuse_faces;
        for (auto fit = current_state.cdt.finite_faces_begin(); fit != current_state.cdt.finite_faces_end(); ++fit) {
            if (is_obtuse_triangle(fit)) {
                obtuse_faces.push_back(fit);
            }
        }
        //Αν δεν εχουμε ουτε ενα τριγωνο με αμβλειες τερματισε
        if (obtuse_faces.empty()) break; 

        // διαλεγουμε ενα τυχαιο τριγωνο με αμβλειες
        uniform_int_distribution<> face_dis(0, obtuse_faces.size() - 1);
        int selected_index = face_dis(gen);
        auto selected_face = obtuse_faces[selected_index];
        //παιρνουμε τις κορυφες του  τριγωνου που επιλεξαμε
        Point a = selected_face->vertex(0)->point();
        Point b = selected_face->vertex(1)->point();
        Point c = selected_face->vertex(2)->point();

        // επιλεγουμε μια τυχαια στρατηγικη απο τις 5
        int strategy = gen() % 5;
        Point steiner = select_steiner_point(a, b, c, strategy, current_state.cdt, convex_hull);
        //ελενχουμε αν το σημείο είναι μέσα η πανω στο boundary
        if (convex_hull.bounded_side(steiner) == CGAL::ON_BOUNDED_SIDE || 
            convex_hull.bounded_side(steiner) == CGAL::ON_BOUNDARY) {
                
            
            // δημιουργουμε νεο state με το steiner point
            State new_state = current_state;
            CDT::Vertex_handle v = new_state.cdt.insert(steiner);
            
            //προχωραμε μονο αν η εισαγωγη ηταν επιτυχεις
            if (v != CDT::Vertex_handle()) {
                new_state.obtuse_count = count_Obtuse_Angles(new_state.cdt);
                new_state.steiner_points++;
                new_state.steiner_locations.push_back(steiner);
                //υπολογιζω την ενεργεια του νεου state
                double new_energy = energy(new_state.cdt, cdt.number_of_vertices());
                double delta_energy = new_energy - best_energy;

                //κρητιριο Metropolis 
                               
                if (delta_energy < 0 || exp(-delta_energy / temperature) > dis(gen)) {
                    current_state = new_state;
                    current_energy = new_energy;
                    
                //αν το καινουργιο state ειναι καλυτερο απο το παλιο αλαλξε τα
                  if (current_energy < best_energy) {
                        best_state = current_state;
                        best_energy = current_energy;
                        best_cdt = current_state.cdt;
                        
                    }
                }
            }
        }

        // αφαιρουμε το τριγωνο απο το obtuse_faces
        obtuse_faces.erase(obtuse_faces.begin() + selected_index);
    }
    //χαμηλωνουμε την θερμοκρασια
    temperature -= 1.0 / 0.95;
}
    //επιστεφουμε το καλυτερο state που βρηκαμε
    return best_state;
}


// Κύρια συνάρτηση
TriangulationResult triangulate(const vector<int> &points_x, const vector<int> &points_y, const vector<int> &region_boundary, const vector<pair<int, int>> &additional_constraints,double alpha, double beta, int L,string& method,bool delaunay)
{
    //Αρχεικοποιουμε το CDT
    CDT cdt;
    //φτιαχνουμε Vector για να αποθηκευσουμε ολα τα points
    vector<Point> points;

    for (size_t i = 0; i < points_x.size(); ++i)
    {
        points.push_back(Point(points_x[i], points_y[i]));
    }
    //δημιουμε το convex hull
    Polygon_2 convex_hull;
    for (size_t i : region_boundary)
    {
        convex_hull.push_back(points[i]);
    }

    // Προσθήκη constraints
    for (size_t i = 0; i < region_boundary.size(); ++i)
    {
        int next = (i + 1) % region_boundary.size();
        cdt.insert_constraint(points[region_boundary[i]], points[region_boundary[next]]);
    }
    //Προσθηκη additional constraints
    for (const auto &constraint : additional_constraints)
    {
        cdt.insert_constraint(points[constraint.first], points[constraint.second]);
    }
    //Μετραμε τα αμβλειγωνια τριγωνα στο CDT
    int best_obtuse = count_Obtuse_Angles(cdt);
    cout << "Initial obtuse angles: " << best_obtuse << endl;
    //αρχικοποιηση για το best_CDT και state
    CDT best_cdt;
    int max_depth = 12000;
    State best_overall_state;
    best_overall_state.obtuse_count = numeric_limits<int>::max();
   if (delaunay){
    //κανουμε την μεθοδο sa
    if (method == "sa") {
        //για καλυτερα αποτελσματα την κανουμε 10 φορες και επιστεφουμε την καλυτερη
    for (size_t i = 0; i < 10; i++) {
        
        State current_best = sa_triangulation(cdt, convex_hull, best_obtuse, best_cdt, alpha ,beta ,L);
        

        if (current_best.obtuse_count < best_overall_state.obtuse_count) {
            best_overall_state = current_best;
            best_cdt = current_best.cdt;
        }
    }
    //καλουμε την μεθοδο local
} else if (method == "local") {
    State initial_state = {cdt, best_obtuse, 0, {}, {}};
    best_overall_state = local_triangulation(cdt, convex_hull,  best_obtuse, best_cdt, max_depth);
    best_cdt = best_overall_state.cdt;
    }
   }
   //αν το delaunay ειναι false τοτε τρεχουμε την πρωτη εργασια
   else{
    old_triangulate(points_x, points_y, region_boundary, additional_constraints);
   }
   //ετοιμαζουμε τα αποτελεσματα
   draw(best_overall_state.cdt);
    TriangulationResult results;
    results.obtuse_count = best_overall_state.obtuse_count;

    for (CDT::Finite_edges_iterator eit = best_overall_state.cdt.finite_edges_begin();
         eit != best_overall_state.cdt.finite_edges_end(); ++eit)
    {  
        CDT::Face_handle face = eit->first;
        int index = eit->second;
         // παιρνουμε τις δυο κορυφες της ακμης
        CDT::Vertex_handle v1 = face->vertex((index + 1) % 3);
        CDT::Vertex_handle v2 = face->vertex((index + 2) % 3);

        // // βρειτε τις θεσεις αυτων των σημειων 
        auto it1 = find(points.begin(), points.end(), v1->point());
        auto it2 = find(points.begin(), points.end(), v2->point());
        // εαν και τα δυο σημεια βρεθουν στην αρχικη εισοδο τα βαζουμε στο αποτελεσμα
        if (it1 != points.end() && it2 != points.end())
        {
            int index1 = distance(points.begin(), it1);
            int index2 = distance(points.begin(), it2);
            results.edges.push_back({index1, index2});
        }
    }
    // Παιρνουμε τα Steiner points
    set<Point> original_points(points.begin(), points.end());
    //ελενχουμε ολα τα τριγωνα και παιρνουμε το καλυτερο
    for (CDT::Finite_vertices_iterator vit = best_overall_state.cdt.finite_vertices_begin();
         vit != best_overall_state.cdt.finite_vertices_end(); ++vit)
    {
        Point p = vit->point();

        // Αν δεν βρισκεται στο original set τότε ειανι steiner point
        if (original_points.find(p) == original_points.end())
        {
            results.steiner_points_x.push_back(CGAL::to_double(p.x()));
            results.steiner_points_y.push_back(CGAL::to_double(p.y()));
        }
    }
    return results;
    draw(best_overall_state.cdt);
}
//////////////////////////////////////////////////////////////////////////