// sistema_imgui.cpp
// Sistema de Gestão de Transportes - ImGui front-end
// Requer: Dear ImGui, GLFW, OpenGL3, MySQL C client
// Adaptado do seu código terminal -> GUI (abas, dark theme)

#include <imgui.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <functional>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <regex>

#include <C:\xampp\mysql\include\mysql.h> 

#if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
#include <GL/gl3w.h>    // Initialize with gl3wInit()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
#include <GL/glew.h>    // Initialize with glewInit()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
#include <glad/glad.h>  // Initialize with gladLoadGL()
#else
#include <glad/glad.h>
#endif

#include <GLFW/glfw3.h> // Will drag system OpenGL headers

// ------------------- Configuração DB (ajuste conforme necessária) -------------------
static const char* DB_HOST = "127.0.0.1";
static const char* DB_USER = "root";
static const char* DB_PASS = "";
static const char* DB_NAME = "banco_transportes";
static unsigned int DB_PORT = 3307; // ajuste se necessário

// ------------------- Helpers de validação (copiados/adaptados) -------------------
static inline std::string trimStr(const std::string &s) {
    size_t l = 0; while (l < s.size() && isspace((unsigned char)s[l])) ++l;
    size_t r = s.size(); while (r > l && isspace((unsigned char)s[r-1])) --r;
    return s.substr(l, r - l);
}
static inline bool isNumber(const std::string& s) {
    if (s.empty()) return false;
    bool seenDigit = false;
    for (char c : s) {
        if (isdigit((unsigned char)c)) { seenDigit = true; continue; }
        if (isspace((unsigned char)c)) continue;
        return false;
    }
    return seenDigit;
}
static inline bool isNumeric(const std::string& s) {
    if (s.empty()) return false;
    bool hasDecimal = false;
    size_t start = 0;
    if (s[0] == '-' || s[0] == '+') start = 1;
    for (size_t i = start; i < s.size(); ++i) {
        if (isdigit((unsigned char)s[i])) continue;
        if (s[i] == '.') { if (hasDecimal) return false; hasDecimal = true; continue; }
        return false;
    }
    return s.size() > start;
}
static inline std::string onlyDigits(const std::string &s) {
    std::string out; for (char c : s) if (isdigit((unsigned char)c)) out.push_back(c); return out;
}
static inline bool validateName(const std::string &s) { auto t = trimStr(s); return t.size() >= 2 && t.size() <= 100; }
static inline bool validateCNH(const std::string &s) { return onlyDigits(s).size() == 11; }
static inline bool validateSalary(const std::string &s) { if (!isNumeric(s)) return false; try { double v = std::stod(s); return v >= 0.0 && v < 1e9; } catch(...) { return false; } }
static inline bool validatePlate(const std::string &s) {
    std::string t; for (char c : s) if (!isspace((unsigned char)c)) t.push_back(toupper((unsigned char)c));
    if (t.size() != 7) return false;
    for (char c : t) if (!isalnum((unsigned char)c)) return false;
    return true;
}
static inline bool validateYear(const std::string &s) {
    if (!isNumber(s)) return false;
    try { int y = std::stoi(s); return y >= 1900 && y <= 2100; } catch(...) { return false; }
}
static inline bool validateKm(const std::string &s) { if (!isNumber(s)) return false; try { long v = std::stol(s); return v >= 0 && v <= 1000000000L; } catch(...) { return false; } }
static inline bool validateCnpjCpf(const std::string &s) { std::string d = onlyDigits(s); return d.size() == 11 || d.size() == 14; }
static inline bool validateDateYMD(const std::string &s) {
    std::regex re(R"(^\d{4}-\d{2}-\d{2}$)"); if (!std::regex_match(s, re)) return false;
    int y=0,m=0,d=0; try { y = stoi(s.substr(0,4)); m = stoi(s.substr(5,2)); d = stoi(s.substr(8,2)); } catch(...) { return false; }
    if (m < 1 || m > 12) return false; int mdays = 31;
    if (m==4||m==6||m==9||m==11) mdays = 30; else if (m==2) {
        bool leap = (y%400==0) || (y%4==0 && y%100!=0); mdays = leap ? 29 : 28;
    }
    return d >=1 && d <= mdays;
}
static inline bool validatePositiveNumber(const std::string &s) { if (!isNumeric(s)) return false; try { return std::stod(s) > 0.0; } catch(...) { return false; } }

// ------------------- MySQL helpers -------------------
static MYSQL* connectDB() {
    MYSQL* conn = mysql_init(NULL);
    if (!conn) return nullptr;
    unsigned int protocol = MYSQL_PROTOCOL_TCP;
    mysql_options(conn, MYSQL_OPT_PROTOCOL, &protocol);
    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, DB_PORT, NULL, 0)) {
        return nullptr;
    }
    return conn;
}
static std::string escapeString(MYSQL* conn, const std::string &s) {
    if (!conn) return "";
    std::vector<char> buf(s.size()*2 + 1);
    unsigned long len = mysql_real_escape_string(conn, buf.data(), s.c_str(), (unsigned long)s.size());
    return std::string(buf.data(), buf.data()+len);
}
static bool checkIdExists(MYSQL* conn, const std::string &table, const std::string &idField, const std::string &idValue) {
    if (!conn) return false;
    std::string q = "SELECT 1 FROM " + table + " WHERE " + idField + " = " + idValue + " LIMIT 1;";
    if (mysql_query(conn, q.c_str())) return false;
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return false;
    bool exists = mysql_num_rows(res) > 0;
    mysql_free_result(res);
    return exists;
}

// Helper para executar SELECT e retornar rows/cols
struct QueryResult {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> cols;
};
static QueryResult runSelect(MYSQL* conn, const std::string &q) {
    QueryResult out;
    if (!conn) return out;
    if (mysql_query(conn, q.c_str())) return out;
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return out;
    int num_fields = mysql_num_fields(res);
    MYSQL_FIELD* fields = mysql_fetch_fields(res);
    for (int i = 0; i < num_fields; ++i) out.cols.push_back(fields[i].name);
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        unsigned long *lengths = mysql_fetch_lengths(res);
        std::vector<std::string> r;
        for (int i = 0; i < num_fields; ++i) {
            r.push_back(row[i] ? row[i] : std::string(""));
        }
        out.rows.push_back(r);
    }
    mysql_free_result(res);
    return out;
}

// ------------------- Utils GUI -------------------
static inline void SmallSeparator() { ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); }

// ------------------- Tab: Motorista -------------------
void RenderMotoristaTab(MYSQL* conn) {
    static char nomeBuf[128] = "";
    static char cnhBuf[32] = "";
    static char salarioBuf[32] = "";
    static char updIdBuf[16] = "";
    static char delIdBuf[16] = "";
    static std::string status = "";

    ImGui::Text("Cadastrar Motorista");
    ImGui::InputText("Nome##cadm", nomeBuf, IM_ARRAYSIZE(nomeBuf));
    ImGui::InputText("CNH##cadm", cnhBuf, IM_ARRAYSIZE(cnhBuf));
    ImGui::InputText("Salário##cadm", salarioBuf, IM_ARRAYSIZE(salarioBuf));
    if (ImGui::Button("Cadastrar")) {
        std::string nome = trimStr(std::string(nomeBuf));
        std::string cnh = trimStr(std::string(cnhBuf));
        std::string salario = trimStr(std::string(salarioBuf));
        if (!validateName(nome)) { status = "Nome inválido (2-100 chars)."; }
        else if (!validateCNH(cnh)) { status = "CNH inválida (11 dígitos)."; }
        else if (!validateSalary(salario)) { status = "Salário inválido."; }
        else if (!conn) { status = "Erro DB: conexão nula."; }
        else {
            std::string q = "INSERT INTO MOTORISTA (NOME, CNH, SALARIO) VALUES ('" + escapeString(conn,nome) + "','" + escapeString(conn,cnh) + "'," + salario + ");";
            if (mysql_query(conn, q.c_str())) status = std::string("Erro ao inserir: ") + mysql_error(conn);
            else { status = "Motorista cadastrado com sucesso."; nomeBuf[0]=cnhBuf[0]=salarioBuf[0]=0; }
        }
    }
    ImGui::TextWrapped("%s", status.c_str());
    SmallSeparator();

    // Listagem
    if (ImGui::Button("Listar Motoristas")) {
        // toggle: we'll display below
    }
    ImGui::SameLine();
    if (ImGui::Button("Atualizar (aplicar)")) {
        std::string id = trimStr(std::string(updIdBuf));
        // This example uses simple update UI: fill fields and click apply
        if (!isNumber(id)) { status = "ID inválido para atualizar."; }
        else {
            std::string setpart;
            std::string n = trimStr(std::string(nomeBuf));
            std::string c = trimStr(std::string(cnhBuf));
            std::string s = trimStr(std::string(salarioBuf));
            if (!n.empty()) setpart += "NOME='" + escapeString(conn, n) + "',";
            if (!c.empty()) setpart += "CNH='" + escapeString(conn, c) + "',";
            if (!s.empty()) setpart += "SALARIO=" + s + ",";
            if (setpart.empty()) status = "Nada para atualizar.";
            else {
                if (setpart.back()==',') setpart.pop_back();
                std::string q = "UPDATE MOTORISTA SET " + setpart + " WHERE ID_MOTORISTA = " + id + ";";
                if (mysql_query(conn, q.c_str())) status = std::string("Erro: ") + mysql_error(conn);
                else status = "Motorista atualizado.";
            }
        }
    }
    ImGui::SameLine();
    ImGui::InputText("ID para update", updIdBuf, IM_ARRAYSIZE(updIdBuf));
    ImGui::InputText("ID para excluir", delIdBuf, IM_ARRAYSIZE(delIdBuf));
    ImGui::SameLine();
    if (ImGui::Button("Excluir")) {
        std::string id = trimStr(std::string(delIdBuf));
        if (!isNumber(id)) status = "ID inválido para exclusão.";
        else {
            std::string q = "DELETE FROM MOTORISTA WHERE ID_MOTORISTA = " + id + ";";
            if (mysql_query(conn, q.c_str())) status = std::string("Erro: ") + mysql_error(conn);
            else if (mysql_affected_rows(conn) > 0) status = "Motorista excluído com sucesso.";
            else status = "Nenhuma linha excluída (ID não encontrado).";
        }
    }
    SmallSeparator();

    // Mostrar tabela (sempre fetch aqui para simplificar)
    if (ImGui::CollapsingHeader("Lista de Motoristas (clique para expandir)")) {
        QueryResult qr = runSelect(conn, "SELECT ID_MOTORISTA, NOME, CNH, SALARIO FROM MOTORISTA;");
        if (qr.cols.empty()) ImGui::Text("Sem resultados ou erro.");
        else {
            ImGui::BeginChild("motorista_table", ImVec2(0,300), true);
            ImGui::Columns((int)qr.cols.size(), "colM");
            for (auto &c : qr.cols) { ImGui::Text("%s", c.c_str()); ImGui::NextColumn(); }
            ImGui::Separator();
            for (auto &r : qr.rows) {
                for (auto &cell : r) { ImGui::Text("%s", cell.c_str()); ImGui::NextColumn(); }
            }
            ImGui::EndChild();
        }
    }
}

// ------------------- Tab: Veículo -------------------
void RenderVeiculoTab(MYSQL* conn) {
    static char placaBuf[32] = "";
    static char modeloBuf[128] = "";
    static char anoBuf[8] = "";
    static char kmBuf[16] = "";
    static char updIdBuf[16] = "";
    static char delIdBuf[16] = "";
    static std::string status = "";

    ImGui::Text("Cadastrar Veículo");
    ImGui::InputText("Placa", placaBuf, IM_ARRAYSIZE(placaBuf));
    ImGui::InputText("Modelo", modeloBuf, IM_ARRAYSIZE(modeloBuf));
    ImGui::InputText("Ano", anoBuf, IM_ARRAYSIZE(anoBuf));
    ImGui::InputText("Quilometragem", kmBuf, IM_ARRAYSIZE(kmBuf));
    if (ImGui::Button("Cadastrar")) {
        std::string placa = trimStr(std::string(placaBuf));
        std::string modelo = trimStr(std::string(modeloBuf));
        std::string ano = trimStr(std::string(anoBuf));
        std::string km = trimStr(std::string(kmBuf));
        if (!validatePlate(placa)) status = "Placa inválida (ex: ABC1234).";
        else if (modelo.empty() || modelo.size()>100) status = "Modelo inválido.";
        else if (!validateYear(ano)) status = "Ano inválido.";
        else if (!validateKm(km)) status = "Quilometragem inválida.";
        else {
            std::string q = "INSERT INTO VEICULO (PLACA, MODELO, ANO, QUILOMETRAGEM) VALUES ('" + escapeString(conn,placa) + "','" + escapeString(conn,modelo) + "'," + ano + "," + km + ");";
            if (mysql_query(conn, q.c_str())) status = std::string("Erro: ") + mysql_error(conn);
            else { status = "Veículo cadastrado."; placaBuf[0]=modeloBuf[0]=anoBuf[0]=kmBuf[0]=0; }
        }
    }
    ImGui::TextWrapped("%s", status.c_str());
    SmallSeparator();

    ImGui::InputText("ID para update", updIdBuf, IM_ARRAYSIZE(updIdBuf));
    ImGui::SameLine();
    if (ImGui::Button("Atualizar (aplicar)")) {
        std::string id = trimStr(std::string(updIdBuf));
        if (!isNumber(id)) status = "ID inválido para update.";
        else {
            std::string setpart;
            std::string placa = trimStr(std::string(placaBuf));
            std::string modelo = trimStr(std::string(modeloBuf));
            std::string ano = trimStr(std::string(anoBuf));
            std::string km = trimStr(std::string(kmBuf));
            if (!placa.empty()) setpart += "PLACA='" + escapeString(conn,placa) + "',";
            if (!modelo.empty()) setpart += "MODELO='" + escapeString(conn,modelo) + "',";
            if (!ano.empty()) setpart += "ANO=" + ano + ",";
            if (!km.empty()) setpart += "QUILOMETRAGEM=" + km + ",";
            if (setpart.empty()) status = "Nada para atualizar.";
            else {
                if (setpart.back()==',') setpart.pop_back();
                std::string q = "UPDATE VEICULO SET " + setpart + " WHERE ID_VEICULO = " + id + ";";
                if (mysql_query(conn, q.c_str())) status = std::string("Erro: ") + mysql_error(conn);
                else status = "Veículo atualizado.";
            }
        }
    }
    ImGui::InputText("ID para excluir##v", delIdBuf, IM_ARRAYSIZE(delIdBuf));
    ImGui::SameLine();
    if (ImGui::Button("Excluir##v")) {
        std::string id = trimStr(std::string(delIdBuf));
        if (!isNumber(id)) status = "ID inválido para exclusão.";
        else {
            std::string q = "DELETE FROM VEICULO WHERE ID_VEICULO = " + id + ";";
            if (mysql_query(conn, q.c_str())) status = std::string("Erro: ") + mysql_error(conn);
            else if (mysql_affected_rows(conn) > 0) status = "Veículo excluído.";
            else status = "Nenhuma linha excluída.";
        }
    }
    SmallSeparator();

    if (ImGui::CollapsingHeader("Lista de Veículos")) {
        QueryResult qr = runSelect(conn, "SELECT ID_VEICULO, PLACA, MODELO, ANO, QUILOMETRAGEM FROM VEICULO;");
        if (qr.cols.empty()) ImGui::Text("Sem resultados ou erro.");
        else {
            ImGui::BeginChild("veiculo_table", ImVec2(0,300), true);
            ImGui::Columns((int)qr.cols.size(), "colV");
            for (auto &c : qr.cols) { ImGui::Text("%s", c.c_str()); ImGui::NextColumn(); }
            ImGui::Separator();
            for (auto &r : qr.rows) { for (auto &cell : r) { ImGui::Text("%s", cell.c_str()); ImGui::NextColumn(); } }
            ImGui::EndChild();
        }
    }
}

// ------------------- Tab: Cliente -------------------
void RenderClienteTab(MYSQL* conn) {
    static char nomeBuf[128] = "";
    static char cnpjBuf[32] = "";
    static char enderecoBuf[256] = "";
    static char updIdBuf[16] = "";
    static char delIdBuf[16] = "";
    static std::string status = "";

    ImGui::Text("Cadastrar Cliente");
    ImGui::InputText("Nome##ccliente", nomeBuf, IM_ARRAYSIZE(nomeBuf));
    ImGui::InputText("CNPJ/CPF##ccliente", cnpjBuf, IM_ARRAYSIZE(cnpjBuf));
    ImGui::InputText("Endereço##ccliente", enderecoBuf, IM_ARRAYSIZE(enderecoBuf));
    if (ImGui::Button("Cadastrar")) {
        std::string nome = trimStr(std::string(nomeBuf));
        std::string cnpj = trimStr(std::string(cnpjBuf));
        std::string endereco = trimStr(std::string(enderecoBuf));
        if (!validateName(nome)) status = "Nome inválido.";
        else if (!validateCnpjCpf(cnpj)) status = "CNPJ/CPF inválido.";
        else if (endereco.empty() || endereco.size()>200) status = "Endereço inválido.";
        else {
            std::string q = "INSERT INTO CLIENTE (NOME, CNPJ_CPF, ENDERECO) VALUES ('" + escapeString(conn,nome) + "','" + escapeString(conn,cnpj) + "','" + escapeString(conn,endereco) + "');";
            if (mysql_query(conn, q.c_str())) status = std::string("Erro: ") + mysql_error(conn);
            else { status = "Cliente cadastrado."; nomeBuf[0]=cnpjBuf[0]=enderecoBuf[0]=0; }
        }
    }
    ImGui::TextWrapped("%s", status.c_str());
    SmallSeparator();

    ImGui::InputText("ID para update##cl", updIdBuf, IM_ARRAYSIZE(updIdBuf));
    ImGui::SameLine();
    if (ImGui::Button("Atualizar (aplicar)##cl")) {
        std::string id = trimStr(std::string(updIdBuf));
        if (!isNumber(id)) status = "ID inválido.";
        else {
            std::string setpart;
            std::string n = trimStr(std::string(nomeBuf));
            std::string c = trimStr(std::string(cnpjBuf));
            std::string e = trimStr(std::string(enderecoBuf));
            if (!n.empty()) setpart += "NOME='" + escapeString(conn,n) + "',";
            if (!c.empty()) setpart += "CNPJ_CPF='" + escapeString(conn,c) + "',";
            if (!e.empty()) setpart += "ENDERECO='" + escapeString(conn,e) + "',";
            if (setpart.empty()) status = "Nada para atualizar.";
            else {
                if (setpart.back()==',') setpart.pop_back();
                std::string q = "UPDATE CLIENTE SET " + setpart + " WHERE ID_CLIENTE = " + id + ";";
                if (mysql_query(conn, q.c_str())) status = std::string("Erro: ") + mysql_error(conn);
                else status = "Cliente atualizado.";
            }
        }
    }

    ImGui::InputText("ID para excluir##cl", delIdBuf, IM_ARRAYSIZE(delIdBuf));
    ImGui::SameLine();
    if (ImGui::Button("Excluir##cl")) {
        std::string id = trimStr(std::string(delIdBuf));
        if (!isNumber(id)) status = "ID inválido.";
        else {
            std::string q = "DELETE FROM CLIENTE WHERE ID_CLIENTE = " + id + ";";
            if (mysql_query(conn,q.c_str())) status = std::string("Erro: ") + mysql_error(conn);
            else if (mysql_affected_rows(conn) > 0) status = "Cliente excluído.";
            else status = "Nenhuma linha excluída.";
        }
    }
    SmallSeparator();

    if (ImGui::CollapsingHeader("Lista de Clientes")) {
        QueryResult qr = runSelect(conn, "SELECT ID_CLIENTE, NOME, CNPJ_CPF, ENDERECO FROM CLIENTE;");
        if (qr.cols.empty()) ImGui::Text("Sem resultados ou erro.");
        else {
            ImGui::BeginChild("cliente_table", ImVec2(0,300), true);
            ImGui::Columns((int)qr.cols.size(), "colC");
            for (auto &c : qr.cols) { ImGui::Text("%s", c.c_str()); ImGui::NextColumn(); }
            ImGui::Separator();
            for (auto &r : qr.rows) { for (auto &cell : r) { ImGui::Text("%s", cell.c_str()); ImGui::NextColumn(); } }
            ImGui::EndChild();
        }
    }
}

// ------------------- Tab: Carga -------------------
void RenderCargaTab(MYSQL* conn) {
    static char descBuf[256] = "";
    static char pesoBuf[32] = "";
    static char valorBuf[32] = "";
    static char idClienteBuf[16] = "";
    static char idRotaBuf[16] = "";
    static char updIdBuf[16] = "";
    static char delIdBuf[16] = "";
    static std::string status = "";

    ImGui::Text("Cadastrar Carga");
    ImGui::InputText("Descrição", descBuf, IM_ARRAYSIZE(descBuf));
    ImGui::InputText("Peso (kg)", pesoBuf, IM_ARRAYSIZE(pesoBuf));
    ImGui::InputText("Valor", valorBuf, IM_ARRAYSIZE(valorBuf));
    ImGui::InputText("ID Cliente", idClienteBuf, IM_ARRAYSIZE(idClienteBuf));
    ImGui::InputText("ID Rota", idRotaBuf, IM_ARRAYSIZE(idRotaBuf));
    if (ImGui::Button("Cadastrar")) {
        std::string desc = trimStr(std::string(descBuf));
        std::string peso = trimStr(std::string(pesoBuf));
        std::string valor = trimStr(std::string(valorBuf));
        std::string idCliente = trimStr(std::string(idClienteBuf));
        std::string idRota = trimStr(std::string(idRotaBuf));
        if (desc.empty() || desc.size()>200) status = "Descrição inválida.";
        else if (!validatePositiveNumber(peso)) status = "Peso inválido.";
        else if (!validatePositiveNumber(valor)) status = "Valor inválido.";
        else if (!isNumber(idCliente) || !checkIdExists(conn,"CLIENTE","ID_CLIENTE",idCliente)) status = "Cliente não encontrado.";
        else if (!isNumber(idRota) || !checkIdExists(conn,"ROTA","ID_ROTA",idRota)) status = "Rota não encontrada.";
        else {
            std::string q = "INSERT INTO CARGA (DESCRICAO, PESO, VALOR, ID_CLIENTE, ID_ROTA) VALUES ('" + escapeString(conn,desc) + "'," + peso + "," + valor + "," + idCliente + "," + idRota + ");";
            if (mysql_query(conn,q.c_str())) status = std::string("Erro: ") + mysql_error(conn);
            else { status = "Carga cadastrada."; descBuf[0]=pesoBuf[0]=valorBuf[0]=idClienteBuf[0]=idRotaBuf[0]=0; }
        }
    }
    ImGui::TextWrapped("%s", status.c_str());
    SmallSeparator();

    ImGui::InputText("ID para update##cd", updIdBuf, IM_ARRAYSIZE(updIdBuf));
    ImGui::SameLine();
    if (ImGui::Button("Atualizar (aplicar)##cd")) {
        std::string id = trimStr(std::string(updIdBuf));
        if (!isNumber(id)) status = "ID inválido.";
        else {
            std::string setpart;
            std::string desc = trimStr(std::string(descBuf));
            std::string peso = trimStr(std::string(pesoBuf));
            std::string valor = trimStr(std::string(valorBuf));
            std::string idCliente = trimStr(std::string(idClienteBuf));
            std::string idRota = trimStr(std::string(idRotaBuf));
            if (!desc.empty()) setpart += "DESCRICAO='" + escapeString(conn,desc) + "',";
            if (!peso.empty()) setpart += "PESO=" + peso + ",";
            if (!valor.empty()) setpart += "VALOR=" + valor + ",";
            if (!idCliente.empty()) setpart += "ID_CLIENTE=" + idCliente + ",";
            if (!idRota.empty()) setpart += "ID_ROTA=" + idRota + ",";
            if (setpart.empty()) status = "Nada para atualizar.";
            else {
                if (setpart.back()==',') setpart.pop_back();
                std::string q = "UPDATE CARGA SET " + setpart + " WHERE ID_CARGA = " + id + ";";
                if (mysql_query(conn,q.c_str())) status = std::string("Erro: ") + mysql_error(conn);
                else status = "Carga atualizada.";
            }
        }
    }

    ImGui::InputText("ID para excluir##cd", delIdBuf, IM_ARRAYSIZE(delIdBuf));
    ImGui::SameLine();
    if (ImGui::Button("Excluir##cd")) {
        std::string id = trimStr(std::string(delIdBuf));
        if (!isNumber(id)) status = "ID inválido.";
        else {
            std::string q = "DELETE FROM CARGA WHERE ID_CARGA = " + id + ";";
            if (mysql_query(conn,q.c_str())) status = std::string("Erro: ") + mysql_error(conn);
            else if (mysql_affected_rows(conn) > 0) status = "Carga excluída.";
            else status = "Nenhuma linha excluída.";
        }
    }
    SmallSeparator();

    if (ImGui::CollapsingHeader("Lista de Cargas")) {
        QueryResult qr = runSelect(conn, "SELECT ID_CARGA, DESCRICAO, PESO, VALOR, ID_CLIENTE, ID_ROTA FROM CARGA;");
        if (qr.cols.empty()) ImGui::Text("Sem resultados ou erro.");
        else {
            ImGui::BeginChild("carga_table", ImVec2(0,300), true);
            ImGui::Columns((int)qr.cols.size(), "colCarga");
            for (auto &c : qr.cols) { ImGui::Text("%s", c.c_str()); ImGui::NextColumn(); }
            ImGui::Separator();
            for (auto &r : qr.rows) { for (auto &cell : r) { ImGui::Text("%s", cell.c_str()); ImGui::NextColumn(); } }
            ImGui::EndChild();
        }
    }
}

// ------------------- Tab: Rota -------------------
void RenderRotaTab(MYSQL* conn) {
    static char origemBuf[128] = "";
    static char destinoBuf[128] = "";
    static char distanciaBuf[32] = "";
    static char idMotoristaBuf[16] = "";
    static char idVeiculoBuf[16] = "";
    static char updIdBuf[16] = "";
    static char delIdBuf[16] = "";
    static std::string status = "";

    ImGui::Text("Cadastrar Rota");
    ImGui::InputText("Origem", origemBuf, IM_ARRAYSIZE(origemBuf));
    ImGui::InputText("Destino", destinoBuf, IM_ARRAYSIZE(destinoBuf));
    ImGui::InputText("Distância (km)", distanciaBuf, IM_ARRAYSIZE(distanciaBuf));
    ImGui::InputText("ID Motorista", idMotoristaBuf, IM_ARRAYSIZE(idMotoristaBuf));
    ImGui::InputText("ID Veículo", idVeiculoBuf, IM_ARRAYSIZE(idVeiculoBuf));
    if (ImGui::Button("Cadastrar")) {
        std::string origem = trimStr(std::string(origemBuf));
        std::string destino = trimStr(std::string(destinoBuf));
        std::string distancia = trimStr(std::string(distanciaBuf));
        std::string idMotorista = trimStr(std::string(idMotoristaBuf));
        std::string idVeiculo = trimStr(std::string(idVeiculoBuf));
        if (origem.empty()||origem.size()>100) status = "Origem inválida.";
        else if (destino.empty()||destino.size()>100) status = "Destino inválido.";
        else if (!validatePositiveNumber(distancia)) status = "Distância inválida.";
        else if (!isNumber(idMotorista) || !checkIdExists(conn,"MOTORISTA","ID_MOTORISTA",idMotorista)) status = "Motorista não encontrado.";
        else if (!isNumber(idVeiculo) || !checkIdExists(conn,"VEICULO","ID_VEICULO",idVeiculo)) status = "Veículo não encontrado.";
        else {
            std::string q = "INSERT INTO ROTA (ORIGEM, DESTINO, DISTANCIA, ID_MOTORISTA, ID_VEICULO) VALUES ('" + escapeString(conn,origem) + "','" + escapeString(conn,destino) + "'," + distancia + "," + idMotorista + "," + idVeiculo + ");";
            if (mysql_query(conn,q.c_str())) status = std::string("Erro: ") + mysql_error(conn);
            else { status = "Rota cadastrada."; origemBuf[0]=destinoBuf[0]=distanciaBuf[0]=idMotoristaBuf[0]=idVeiculoBuf[0]=0; }
        }
    }
    ImGui::TextWrapped("%s", status.c_str());
    SmallSeparator();

    ImGui::InputText("ID para update##r", updIdBuf, IM_ARRAYSIZE(updIdBuf));
    ImGui::SameLine();
    if (ImGui::Button("Atualizar (aplicar)##r")) {
        std::string id = trimStr(std::string(updIdBuf));
        if (!isNumber(id)) status = "ID inválido.";
        else {
            std::string setpart;
            std::string origem = trimStr(std::string(origemBuf));
            std::string destino = trimStr(std::string(destinoBuf));
            std::string distancia = trimStr(std::string(distanciaBuf));
            std::string idMotorista = trimStr(std::string(idMotoristaBuf));
            std::string idVeiculo = trimStr(std::string(idVeiculoBuf));
            if (!origem.empty()) setpart += "ORIGEM='" + escapeString(conn,origem) + "',";
            if (!destino.empty()) setpart += "DESTINO='" + escapeString(conn,destino) + "',";
            if (!distancia.empty()) setpart += "DISTANCIA=" + distancia + ",";
            if (!idMotorista.empty()) setpart += "ID_MOTORISTA=" + idMotorista + ",";
            if (!idVeiculo.empty()) setpart += "ID_VEICULO=" + idVeiculo + ",";
            if (setpart.empty()) status = "Nada para atualizar.";
            else {
                if (setpart.back()==',') setpart.pop_back();
                std::string q = "UPDATE ROTA SET " + setpart + " WHERE ID_ROTA = " + id + ";";
                if (mysql_query(conn,q.c_str())) status = std::string("Erro: ") + mysql_error(conn);
                else status = "Rota atualizada.";
            }
        }
    }

    ImGui::InputText("ID para excluir##r", delIdBuf, IM_ARRAYSIZE(delIdBuf));
    ImGui::SameLine();
    if (ImGui::Button("Excluir##r")) {
        std::string id = trimStr(std::string(delIdBuf));
        if (!isNumber(id)) status = "ID inválido.";
        else {
            std::string q = "DELETE FROM ROTA WHERE ID_ROTA = " + id + ";";
            if (mysql_query(conn,q.c_str())) status = std::string("Erro: ") + mysql_error(conn);
            else if (mysql_affected_rows(conn) > 0) status = "Rota excluída.";
            else status = "Nenhuma linha excluída.";
        }
    }
    SmallSeparator();

    if (ImGui::CollapsingHeader("Lista de Rotas")) {
        QueryResult qr = runSelect(conn, "SELECT ID_ROTA, ORIGEM, DESTINO, DISTANCIA, ID_MOTORISTA, ID_VEICULO FROM ROTA;");
        if (qr.cols.empty()) ImGui::Text("Sem resultados ou erro.");
        else {
            ImGui::BeginChild("rota_table", ImVec2(0,300), true);
            ImGui::Columns((int)qr.cols.size(), "colR");
            for (auto &c : qr.cols) { ImGui::Text("%s", c.c_str()); ImGui::NextColumn(); }
            ImGui::Separator();
            for (auto &r : qr.rows) { for (auto &cell : r) { ImGui::Text("%s", cell.c_str()); ImGui::NextColumn(); } }
            ImGui::EndChild();
        }
    }
}

// ------------------- Tab: Manutenção -------------------
void RenderManutencaoTab(MYSQL* conn) {
    static char idVeicBuf[16] = "";
    static char dataBuf[16] = "";
    static char custoBuf[32] = "";
    static char descBuf[256] = "";
    static char updIdBuf[16] = "";
    static char delIdBuf[16] = "";
    static std::string status = "";

    ImGui::Text("Registrar Manutenção");
    ImGui::InputText("ID Veículo", idVeicBuf, IM_ARRAYSIZE(idVeicBuf));
    ImGui::InputText("Data (YYYY-MM-DD)", dataBuf, IM_ARRAYSIZE(dataBuf));
    ImGui::InputText("Custo", custoBuf, IM_ARRAYSIZE(custoBuf));
    ImGui::InputText("Descrição", descBuf, IM_ARRAYSIZE(descBuf));
    if (ImGui::Button("Registrar")) {
        std::string idVeic = trimStr(std::string(idVeicBuf));
        std::string data = trimStr(std::string(dataBuf));
        std::string custo = trimStr(std::string(custoBuf));
        std::string desc = trimStr(std::string(descBuf));
        if (!isNumber(idVeic) || !checkIdExists(conn,"VEICULO","ID_VEICULO",idVeic)) status = "Veículo não encontrado.";
        else if (!validateDateYMD(data)) status = "Data inválida.";
        else if (!validatePositiveNumber(custo)) status = "Custo inválido.";
        else if (desc.empty() || desc.size()>300) status = "Descrição inválida.";
        else {
            std::string q = "INSERT INTO MANUTENCAO (ID_VEICULO, DATA, CUSTO, DESCRICAO) VALUES (" + idVeic + ",'" + escapeString(conn,data) + "'," + custo + ",'" + escapeString(conn,desc) + "');";
            if (mysql_query(conn,q.c_str())) status = std::string("Erro: ") + mysql_error(conn);
            else { status = "Manutenção registrada."; idVeicBuf[0]=dataBuf[0]=custoBuf[0]=descBuf[0]=0; }
        }
    }
    ImGui::TextWrapped("%s", status.c_str());
    SmallSeparator();

    ImGui::InputText("ID para update##m", updIdBuf, IM_ARRAYSIZE(updIdBuf));
    ImGui::SameLine();
    if (ImGui::Button("Atualizar (aplicar)##m")) {
        std::string id = trimStr(std::string(updIdBuf));
        if (!isNumber(id)) status = "ID inválido.";
        else {
            std::string setpart;
            std::string data = trimStr(std::string(dataBuf));
            std::string custo = trimStr(std::string(custoBuf));
            std::string desc = trimStr(std::string(descBuf));
            std::string idVeic = trimStr(std::string(idVeicBuf));
            if (!data.empty()) setpart += "DATA='" + escapeString(conn,data) + "',";
            if (!custo.empty()) setpart += "CUSTO=" + custo + ",";
            if (!desc.empty()) setpart += "DESCRICAO='" + escapeString(conn,desc) + "',";
            if (!idVeic.empty()) setpart += "ID_VEICULO=" + idVeic + ",";
            if (setpart.empty()) status = "Nada para atualizar.";
            else {
                if (setpart.back()==',') setpart.pop_back();
                std::string q = "UPDATE MANUTENCAO SET " + setpart + " WHERE ID_MANUTENCAO = " + id + ";";
                if (mysql_query(conn,q.c_str())) status = std::string("Erro: ") + mysql_error(conn);
                else status = "Manutenção atualizada.";
            }
        }
    }

    ImGui::InputText("ID para excluir##m", delIdBuf, IM_ARRAYSIZE(delIdBuf));
    ImGui::SameLine();
    if (ImGui::Button("Excluir##m")) {
        std::string id = trimStr(std::string(delIdBuf));
        if (!isNumber(id)) status = "ID inválido.";
        else {
            std::string q = "DELETE FROM MANUTENCAO WHERE ID_MANUTENCAO = " + id + ";";
            if (mysql_query(conn,q.c_str())) status = std::string("Erro: ") + mysql_error(conn);
            else if (mysql_affected_rows(conn) > 0) status = "Manutenção excluída.";
            else status = "Nenhuma linha excluída.";
        }
    }
    SmallSeparator();

    if (ImGui::CollapsingHeader("Lista de Manutenções")) {
        QueryResult qr = runSelect(conn, "SELECT ID_MANUTENCAO, ID_VEICULO, DATA, CUSTO, DESCRICAO FROM MANUTENCAO;");
        if (qr.cols.empty()) ImGui::Text("Sem resultados ou erro.");
        else {
            ImGui::BeginChild("manut_table", ImVec2(0,300), true);
            ImGui::Columns((int)qr.cols.size(), "colMnt");
            for (auto &c : qr.cols) { ImGui::Text("%s", c.c_str()); ImGui::NextColumn(); }
            ImGui::Separator();
            for (auto &r : qr.rows) { for (auto &cell : r) { ImGui::Text("%s", cell.c_str()); ImGui::NextColumn(); } }
            ImGui::EndChild();
        }
    }
}

// ------------------- Main -------------------
int main(int, char**) {
    // Inicializa MySQL library
    if (mysql_library_init(0, NULL, NULL)) {
        fprintf(stderr, "Erro: mysql_library_init falhou\n");
        return 1;
    }

    // --- GLFW + OpenGL + ImGui init ---
    if (!glfwInit()) return 1;
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Sistema de Gestão de Transportes - GUI", NULL, NULL);
    if (window == NULL) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    // Init GL loader (glad chosen; alternate init may be necessary depending on your platform)
#if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
    gl3wInit();
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
    glewInit();
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
    gladLoadGL();
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO(); (void)io;

    ImGui::StyleColorsDark(); // você pediu Dark Modern

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Conectar DB (mantém conexão durante execução)
    MYSQL* conn = connectDB();
    if (!conn) {
        fprintf(stderr, "Aviso: não foi possível conectar ao banco de dados com as credenciais atuais.\n");
        // prossegue a GUI, mas operações DB falharão até que o usuário ajuste a configuração no código
    }

    // estado da UI
    int current_tab = 0; // 0 = menu principal

    // loop principal
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Main window com abas
        ImGui::Begin("Sistema de Gestão de Transportes", NULL, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoCollapse);
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Tabelas")) {
                if (ImGui::MenuItem("Motoristas")) current_tab = 1;
                if (ImGui::MenuItem("Veículos")) current_tab = 2;
                if (ImGui::MenuItem("Clientes")) current_tab = 3;
                if (ImGui::MenuItem("Cargas")) current_tab = 4;
                if (ImGui::MenuItem("Rotas")) current_tab = 5;
                if (ImGui::MenuItem("Manutenções")) current_tab = 6;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Conexão")) {
                if (ImGui::MenuItem("Reabrir conexão DB")) {
                    if (conn) mysql_close(conn);
                    conn = connectDB();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // abas
        if (ImGui::BeginTabBar("MainTabBar")) {
            if (ImGui::BeginTabItem("Início")) {
                ImGui::Text("Bem-vindo ao Sistema de Gestão de Transportes (GUI)");
                ImGui::Text("Use as abas para gerenciar Motoristas, Veículos, Clientes, Cargas, Rotas e Manutenções.");
                ImGui::Text("Conexão DB: %s", conn ? "OK" : "NÃO CONECTADO");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Motoristas")) { RenderMotoristaTab(conn); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Veículos")) { RenderVeiculoTab(conn); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Clientes")) { RenderClienteTab(conn); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Cargas")) { RenderCargaTab(conn); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Rotas")) { RenderRotaTab(conn); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Manutenções")) { RenderManutencaoTab(conn); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }

        ImGui::End();

        // Render
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.09f, 0.09f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // cleanup
    if (conn) mysql_close(conn);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    mysql_library_end();
    return 0;
}
