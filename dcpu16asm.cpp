#include <iostream>
#include <iomanip>
#include <boost/algorithm/string.hpp>
#include <vector>
#include <sstream>
#include <map>
#include <cstdio>


std::pair<uint16_t, int> operand_code(const std::string&);
int get_literal_value(const std::string&);
void error_die(const char*, int);

int main()
{
	std::map<std::string, uint16_t> opcodes = 
		{
			{"set", 0x01},
			{"add", 0x02},
			{"sub", 0x03},
			{"mul", 0x04},
			{"div", 0x05},
			{"mod", 0x06},
			{"shl", 0x07},
			{"shr", 0x08},
			{"and", 0x09},
			{"bor", 0x0a},
			{"xor", 0x0b},
			{"ife", 0x0c},
			{"ifn", 0x0d},
			{"ifg", 0x0e},
			{"ifb", 0x0f},
			{"jsr", 0x10},
		};

	std::vector<uint16_t> code;
	std::string line;
	std::map<std::string, std::pair<int, std::vector<int> > > label_info;
	int lineno = 0;
	
	while(getline(std::cin, line)){
		++lineno;

		// Ignore comments, leading and trailing whitespace
		size_t comment_start = line.find_first_of(";");
		line = line.substr(0, comment_start);
		boost::algorithm::trim(line);
		if(line.empty()) continue;

		// Read instruction name, operands and (optional) label name
		std::istringstream ls(line);
		std::string label_name, instruction_name, op1, op2;

		if(line[0] == ':'){
			ls >> label_name;
			label_name = label_name.substr(1);
			label_info[label_name].first = code.size();
		}

		ls >> instruction_name;
		boost::algorithm::to_lower(instruction_name);
		uint16_t opcode = 0;
		auto i = opcodes.find(instruction_name);
		if(i == opcodes.end()) error_die("invalid instruction", lineno);
	 	opcode = i -> second;
		
		std::string tmp;
		std::getline(ls, tmp);

		auto label_ref = [&label_info, code](const std::string& label_name){
				if(label_info.find(label_name) == label_info.end()) label_info[label_name] = {-1, {}};
				label_info[label_name].second.push_back(code.size()+1);
			};

		auto handle_operand = [&label_info, code, &label_ref](const std::string& o){
			std::string op = o;
			boost::algorithm::trim(op); boost::algorithm::to_lower(op);
			auto oc = operand_code(op);
			if(oc.first > 0x3f){
				label_ref(op);
				oc.first = 0x1f;
				oc.second = 0xff;
			}
			return oc;
		};

		try
		{
			if(opcode == 0x00 || opcode > 0x0f)
			{
				auto oc = handle_operand(tmp);	
				uint16_t instruction = opcode;
				instruction |= (oc.first << 4+6);
				code.push_back(instruction);
				if(oc.second >=0) code.push_back(oc.second);
			}
			else
			{
				size_t comma_pos = tmp.find_first_of(",");
				if(comma_pos == std::string::npos) error_die("expected two comma-separated operands", lineno);
				uint16_t instruction = opcode;
				op1 = tmp.substr(0, comma_pos); op2 = tmp.substr(comma_pos + 1);
				auto oc1 = handle_operand(op1);
				auto oc2 = handle_operand(op2);
				instruction |= (oc1.first << 4);
				instruction |= (oc2.first << (4+6));
				code.push_back(instruction);
				if(oc1.second > 0) code.push_back(oc1.second);
				if(oc2.second > 0) code.push_back(oc2.second);
			}
		}
		catch(const std::runtime_error& e)
		{
			error_die(e.what(), lineno);
			return 1;
		}
	}

	// Insert label addresses 
	std::for_each(label_info.begin(), label_info.end(),
		      [&code](const std::pair<std::string, std::pair<int, std::vector<int> > >& a)
		      {
		      	if(a.second.first == -1) error_die(("Undefined label " + a.first).c_str(), -1);
			else std::for_each(a.second.second.begin(), a.second.second.end(), [&code, a](int loc){ code[loc] = a.second.first; });
		      });

	// Output binary
	std::cout.write(reinterpret_cast<char*>(&code[0]), code.size() * sizeof(uint16_t));
	return 0;
}

std::pair<uint16_t, int> operand_code(const std::string& str)
{
	static std::map<std::string, uint16_t> reg_code =
	{
		{"a",0x00},
		{"b",0x01},
		{"c",0x02},
		{"x",0x03},
		{"y",0x04},
		{"z",0x05},
		{"i",0x06},
		{"j",0x07},
		{"[a]",0x08},
		{"[b]",0x09},
		{"[c]",0x0a},
		{"[x]",0x0b},
		{"[y]",0x0c},
		{"[z]",0x0d},
		{"[i]",0x0e},
		{"[j]",0x0f},
		{"pop",  0x18},
		{"peek", 0x19},
		{"push", 0x1a},
		{"sp",   0x1b},
		{"pc",   0x1c},
		{"o",    0x1d},
	};
	static int  no_next_word = -1;
	static std::pair<uint16_t, int> label_result = {0x40, no_next_word};

	auto i = reg_code.find(str);
	if(i != reg_code.end()) return {i->second, no_next_word};
	
	// Remaining cases:
	// [word + register]
	// [word]
	// literal
	// label
	if(str[0] == '[' && str[str.length() - 1] == ']'){
		size_t plus;
		if((plus = str.find_first_of("+", 1)) != std::string::npos){
			std::string literal = str.substr(1, plus - 1); boost::algorithm::trim(literal);
			std::string reg = str.substr(plus + 1, str.length() - plus - 2); boost::algorithm::trim(reg);
			if(reg.length() != 1) throw std::runtime_error("Expected register");
			auto i = reg_code.find(reg);
			if(i == reg_code.end() || i -> second > 0x07) throw std::runtime_error("Expected register");
			uint16_t operand_code = 0x10 + i -> second;
			int literal_value = get_literal_value(literal);
			if(literal_value < 0) throw std::runtime_error("Invalid literal");
			return {operand_code, static_cast<uint16_t>(literal_value)};
		}
		else{
			std::string literal = str.substr(1, str.length() - 2); boost::algorithm::trim(literal);
			int literal_value = get_literal_value(literal);
			if(literal_value < 0) throw std::runtime_error("Invalid literal"); 
			return {0x1e, static_cast<uint16_t>(literal_value)};
		}
	}
	else {
		int literal_value = get_literal_value(str);
		if(literal_value >= 0){
			if(literal_value <= 0x1f) return {0x20 + literal_value, no_next_word};
			return {0x1f, literal_value};
		}
		return label_result; // we have to conclude that it's a label reference
	}
}

int get_literal_value(const std::string& str)
{
	// Get the value of a 16-bit unsigned literal
	int val = 0xdeadbeef;
        if(str.substr(0, 2) == "0x") { if(sscanf(str.c_str(), "%x", &val) < 1) return -1; }
	else if(sscanf(str.c_str(), "%u", &val) < 1 ) { return -1; }

	if(val >= std::numeric_limits<uint16_t>::min() && val <= std::numeric_limits<uint16_t>::max()) return val; 
	else return -1;
}

void error_die(const char* msg, int line)
{
	std::cerr << "Error: ";
	if(line > 0) std::cerr << "on line " << line << ": ";
	std::cerr << msg << std::endl;
	exit(1);
}

