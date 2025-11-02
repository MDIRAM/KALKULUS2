#include <SFML/Graphics.hpp>
#include <cmath>
#include <cctype>
#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>

// ---------------- Tokenizer ----------------
struct Token {
    enum Type { NUM, VAR, OP, FUN, LP, RP, END } t;
    std::string s;
    double v{};
};

struct Lexer {
    std::string src;
    size_t i = 0;
    explicit Lexer(const std::string& s): src(s) {}
    static bool isfunchar(char c){ return std::isalpha((unsigned char)c); }

    std::vector<Token> run(){
        std::vector<Token> out;
        while (i < src.size()) {
            char c = src[i];
            if (std::isspace((unsigned char)c)) { ++i; continue; }
            if (std::isdigit((unsigned char)c) || c=='.') {
                size_t j = i; bool dot = (c=='.'); ++i;
                while (i < src.size() && (std::isdigit((unsigned char)src[i]) || (!dot && src[i]=='.'))) {
                    if (src[i]=='.') dot = true; ++i;
                }
                out.push_back({Token::NUM, "", std::stod(src.substr(j, i-j))});
                continue;
            }
            if (c=='x' || c=='X') { ++i; out.push_back({Token::VAR,"x",0}); continue; }
            if (c=='+'||c=='-'||c=='*'||c=='/'||c=='^'){ ++i; out.push_back({Token::OP,std::string(1,c),0}); continue; }
            if (c=='('){ ++i; out.push_back({Token::LP,"(",0}); continue; }
            if (c==')'){ ++i; out.push_back({Token::RP,")",0}); continue; }
            if (isfunchar(c)){
                size_t j=i; ++i; while (i<src.size() && isfunchar(src[i])) ++i;
                out.push_back({Token::FUN, src.substr(j,i-j), 0});
                continue;
            }
            ++i; // abaikan karakter aneh
        }
        out.push_back({Token::END,"",0});

        // sisipkan perkalian implisit: [NUM|VAR|RP][NUM|VAR|LP|FUN]
        std::vector<Token> z; z.reserve(out.size()*2);
        for (size_t k=0; k+1<out.size(); ++k){
            z.push_back(out[k]);
            bool L = (out[k].t==Token::NUM || out[k].t==Token::VAR || out[k].t==Token::RP);
            bool R = (out[k+1].t==Token::NUM || out[k+1].t==Token::VAR || out[k+1].t==Token::LP || out[k+1].t==Token::FUN);
            if (L && R) z.push_back(Token{Token::OP,"*",0});
        }
        z.push_back(out.back());
        return z;
    }
};

// ---------------- Parser (recursive-descent) ----------------
struct Parser {
    std::vector<Token> t; size_t p=0;
    explicit Parser(const std::vector<Token>& toks): t(toks) {}
    const Token& peek(){ return t[p]; }
    const Token& get(){ return t[p++]; }

    double parse(double x){ p=0; return expr(x); }
    double expr(double x){
        double v = term(x);
        while (peek().t==Token::OP && (peek().s=="+" || peek().s=="-")){
            std::string op=get().s; double r=term(x);
            v = (op=="+")? v+r : v-r;
        }
        return v;
    }
    double term(double x){
        double v = power(x);
        while (peek().t==Token::OP && (peek().s=="*" || peek().s=="/")){
            std::string op=get().s; double r=power(x);
            v = (op=="*")? v*r : v/r;
        }
        return v;
    }
    double power(double x){
        double v = unary(x);
        if (peek().t==Token::OP && peek().s=="^"){
            get(); double r = power(x); // right-assoc
            v = std::pow(v,r);
        }
        return v;
    }
    double unary(double x){
        if (peek().t==Token::OP && (peek().s=="+" || peek().s=="-")){
            std::string op=get().s; double v=unary(x);
            return (op=="+")? v : -v;
        }
        return factor(x);
    }
    double factor(double x){
        if (peek().t==Token::NUM){ double v=get().v; return v; }
        if (peek().t==Token::VAR){ get(); return x; }
        if (peek().t==Token::FUN){
            std::string f = get().s;
            if (peek().t!=Token::LP) throw std::runtime_error("Butuh '(' setelah " + f);
            get(); double v = expr(x);
            if (peek().t!=Token::RP) throw std::runtime_error("Kurung tidak seimbang");
            get();
            for (auto& c: f) c = std::tolower((unsigned char)c);
            if (f=="sin")  return std::sin(v);
            if (f=="cos")  return std::cos(v);
            if (f=="tan")  return std::tan(v);
            if (f=="asin") return std::asin(v);
            if (f=="acos") return std::acos(v);
            if (f=="atan") return std::atan(v);
            if (f=="exp")  return std::exp(v);
            if (f=="ln"||f=="log") return std::log(v);
            if (f=="sqrt") return std::sqrt(v);
            if (f=="abs")  return std::fabs(v);
            throw std::runtime_error("Fungsi tidak dikenali: " + f);
        }
        if (peek().t==Token::LP){
            get(); double v = expr(x);
            if (peek().t!=Token::RP) throw std::runtime_error("Kurung tidak seimbang");
            get(); return v;
        }
        throw std::runtime_error("Token tidak valid");
    }
};

// ---------------- Gambar ----------------
static inline sf::Vector2f toScreen(double x, double y, const sf::Vector2f& origin, float scale){
    return { origin.x + float(x)*scale, origin.y - float(y)*scale };
}

int main(){
    // ===== Input dari terminal =====
    std::string expr;
    double xmin, xmax;
    std::cout << "Masukkan fungsi f(x) (contoh: cos(x^2) atau exp(-x^2)*cos(5x))\n> ";
    std::getline(std::cin, expr);
    std::cout << "Masukkan xmin xmax (misal: -10 10)\n> ";
    std::cin >> xmin >> xmax;
    if (!(xmin < xmax)) { std::cerr << "Range x tidak valid.\n"; return 1; }

    // Build parser
    Parser parser(Lexer(expr).run());

    // ===== Window =====
    const int W = 1200, H = 700;
    sf::RenderWindow win(sf::VideoMode(W,H), "Plot f(x) dari terminal - SFML");
    win.setFramerateLimit(60);

    // Skala: 1 unit = scale piksel. Set agar seluruh [xmin,xmax] pas ke lebar window.
    float scale = float(W) / float(xmax - xmin);
    // Origin.x sehingga x=xmin berada di x=0 layar
    sf::Vector2f origin(-float(xmin)*scale, H/2.f);

    // ===== Siapkan kurva =====
    sf::VertexArray curve(sf::LineStrip);
    auto rebuild = [&](){
        curve.clear();
        // sampling step adaptif berdasar piksel
        const float minStepPx = 1.2f;
        int maxPts = int(W/minStepPx) + 5;
        double step = (xmax - xmin) / double(maxPts);

        for (double x = xmin; x <= xmax; x += step){
            double y;
            try{
                y = parser.parse(x);
                if (!std::isfinite(y)) continue;
            } catch(...) { continue; }
            curve.append(sf::Vertex(toScreen(x,y,origin,scale), sf::Color(200,0,0)));
        }
    };

    // ===== Grid + sumbu =====
    auto drawGridAxes = [&](sf::RenderTarget& rt){
        // unit grid nyaman (>= 25 px)
        float unit = 1.f;
        while (unit*scale < 25.f) unit *= 2.f;
        while (unit*scale > 200.f) unit *= 0.5f;

        double xLeft  = xmin;
        double xRight = xmax;
        double yTop   =  (origin.y - 0) / scale;
        double yBot   =  (origin.y - H) / scale;

        sf::VertexArray grid(sf::Lines);
        for (double x = std::floor(xLeft/unit)*unit; x <= xRight; x += unit){
            sf::Vector2f a = toScreen(x, yTop, origin, scale);
            sf::Vector2f b = toScreen(x, yBot, origin, scale);
            sf::Color c(230,230,230);
            if (std::abs(std::fmod(x, unit*5)) < 1e-6) c = sf::Color(210,210,210);
            grid.append(sf::Vertex(a,c)); grid.append(sf::Vertex(b,c));
        }
        for (double y = std::floor(yBot/unit)*unit; y <= yTop; y += unit){
            sf::Vector2f a = toScreen(xLeft, y, origin, scale);
            sf::Vector2f b = toScreen(xRight, y, origin, scale);
            sf::Color c(230,230,230);
            if (std::abs(std::fmod(y, unit*5)) < 1e-6) c = sf::Color(210,210,210);
            grid.append(sf::Vertex(a,c)); grid.append(sf::Vertex(b,c));
        }
        rt.draw(grid);

        sf::VertexArray axes(sf::Lines, 4);
        axes[0].position = toScreen(xLeft, 0, origin, scale);
        axes[1].position = toScreen(xRight,0, origin, scale);
        axes[2].position = toScreen(0, yBot, origin, scale);
        axes[3].position = toScreen(0, yTop, origin, scale);
        for (int i=0;i<4;++i) axes[i].color = sf::Color::Black;
        rt.draw(axes);
    };

    rebuild();

    // ===== Loop gambar (statik; tidak ada input GUI) =====
    while (win.isOpen()){
        sf::Event e;
        while (win.pollEvent(e)){
            if (e.type==sf::Event::Closed) win.close();
        }
        win.clear(sf::Color::White);
        drawGridAxes(win);
        if (curve.getVertexCount()>1) win.draw(curve);
        win.display();
    }
    return 0;
}