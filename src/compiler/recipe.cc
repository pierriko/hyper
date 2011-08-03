#include <boost/bind.hpp>

#include <compiler/ability.hh>
#include <compiler/depends.hh>
#include <compiler/extract_symbols.hh>
#include <compiler/recipe.hh>
#include <compiler/recipe_parser.hh>
#include <compiler/recipe_expression.hh>
#include <compiler/scope.hh>
#include <compiler/task.hh>
#include <compiler/universe.hh>

#include <compiler/condition_output.hh>
#include <compiler/extension.hh>
#include <compiler/logic_expression_output.hh>
#include <compiler/exec_expression_output.hh>
#include <compiler/output.hh>

using namespace hyper::compiler;

std::pair<bool, std::string> 
is_tagged_func_call(const expression_ast& e, const std::string& name, const universe &u);

std::string symbolList_to_vectorType(const symbolList& syms, const typeList& tList)
{
	std::ostringstream oss;
	oss << "boost::fusion::vector<";
	symbolList::const_iterator it = syms.begin();
	while (it != syms.end()) {
		type t = tList.get(it->second.t);
		oss << t.type_name();
		++it;
		if (it != syms.end())
			oss << ", ";
	}
	oss << ">";

	return oss.str();
}

size_t symbolList_index(const symbolList& sym, const std::string& current_sym)
{
	symbolList::const_iterator it = sym.find(current_sym);
	assert(it != sym.end());
	return std::distance(sym.begin(), it);
}

struct validate_expression
{
	bool &b;
	const ability& a;
	const universe &u;

	validate_expression(bool & b_, const ability& a_, const universe& u_) : 
		b(b_), a(a_), u(u_) {}

	void operator() (const expression_ast& e) const
	{
		b = e.is_valid_predicate(a, u, boost::none) && b;
	}
};

struct valid_unification
{
	bool & b;
	const ability& a;
	const universe& u;
	const symbolList& local;
	boost::optional<std::string> dst;

	valid_unification(bool& b, const ability& a, const universe& u, 
					  const symbolList& local,
					  const boost::optional<std::string>& dst) :
		b(b), a(a), u(u), local(local), dst(dst)
	{}

	void operator() (const unification_expression& p)
	{
		if (!dst) {
			b = false;
			return;
		}

		boost::optional<typeId> t1, t2;
		t1 = u.typeOf(a, p.first, local);
		t2 = u.typeOf(a, p.second, local);

		b = b && t1 && t2 && (*t1 == *t2);
		if (!b)
			return;

		/* Check there are at least one symbol, and that it modifies one
		 * element of dst */
		const std::string *s1, *s2;
		s1 = boost::get<std::string>(&p.first.expr);
		s2 = boost::get<std::string>(&p.second.expr);

		if (!s1 && !s2) {
			b = false;
			std::cerr << "Invalid unification_expression : no symbol " << p << std::endl;
			return;
		}

		bool b1 = s1 && scope::get_scope(*s1) == *dst;
		bool b2 = s2 && scope::get_scope(*s2) == *dst;

		/* One symbol exactly must be part of the dst */
		if (! b1 ^ b2 ) {
			b = false;
			std::cerr << "Invalid unification_expression : no " << *dst << " symbol ";
			std::cerr << p << std::endl;
			return;
		}
	}
};

struct validate_recipe_expression_ : public boost::static_visitor<bool>
{
	const ability& a;
	const universe& u;
	symbolList& local;

	validate_recipe_expression_(const ability& a_, const universe& u_,
							    symbolList& local_) : 
		a(a_), u(u_), local(local_) {}

	bool operator() (empty) const { assert(false); return false; }

	bool operator() (const wait_decl& w) const {
		return w.content.is_valid_predicate(a, u, local);
	}

	template <recipe_op_kind kind>
	bool operator() (const recipe_op<kind>& op) const {
		std::vector<remote_constraint>::const_iterator it;
		bool valid = true;
		for (it = op.content.begin(); it != op.content.end(); ++it) {
			valid &= (it->dst && it->logic_expr.main.is_valid_predicate(a, u, local));
			std::for_each(it->logic_expr.unification_clauses.begin(),
						  it->logic_expr.unification_clauses.end(),
						  valid_unification(valid, a, u, local, it->dst));
		}

		return valid;
	}

	bool operator() (const expression_ast& e) const {
		return e.is_valid(a, u, local);
	}

	bool operator() (const abort_decl& abort) const {
		std::pair<bool, symbol> p = local.get(abort.identifier);
		if (!p.first) {
			std::cerr << "Error : undefined variable " << abort.identifier;
			std::cerr << " in abort clause " << std::endl;
			return false;
		}

		symbol s = p.second;
		if (s.t != u.types().getId("identifier").second) {
			const type& t = u.types().get(s.t);
			std::cerr << "Error : expected a value of type identifier";
			std::cerr << " for variable " << abort.identifier << " in abort clause ";
			std::cerr << " but got a variable of type " << t.name << std::endl;
			return false;
		}

		return true;
	}

	bool operator() (const let_decl& let) const {
		std::pair<bool, symbolACL> p = a.get_symbol(let.identifier);
		if (p.first) {
			std::cerr << "Error : let declaration of " << let.identifier;
			std::cerr << " will shadow a declaration in " << a.name() << std::endl;
			return false;
		}

		std::pair<bool, symbol> p2 = local.get(let.identifier);
		if (p2.first) {
			std::cerr << "Error : let declaration of " << let.identifier;
			std::cerr << " will shadow a local variable " << std::endl;
			return false;
		}

		bool valid = boost::apply_visitor(validate_recipe_expression_(a,u, local),
				let.bounded.expr);
		if (!valid ) 
			return valid;

		/* Get the type of the expression, we only accept to bind valid
		 * expression, with a non-void return type */
		boost::optional<typeId> t = u.typeOf(a, let.bounded, local);
		if (!t || *t == u.types().getId("void").second)
			return false;

		symbolList::add_result res = local.add(let.identifier, *t);
		return res.first;
	}

	bool operator() (const set_decl& set) const {
		std::string identifier = set.identifier;
		std::string scope = scope::get_scope(identifier);
		if (scope != "" && scope != a.name()) {
			std::cerr << "Can't set the remote value : " << identifier << std::endl;
			std::cerr << "Use make if you want to constraint a remote value" << std::endl;
			return false;
		}

		boost::optional<symbol> s = boost::none;
		if (scope == "") {
			std::pair<bool, symbol> p2 = local.get(set.identifier);
			if (p2.first)
				s = p2.second;
		} else {
			std::pair<std::string, std::string> p = scope::decompose(identifier);
			identifier = p.second;
		}

		std::pair<bool, symbolACL> p = a.get_symbol(identifier);
		if (p.first)
			s = p.second.s;

		if (! s) {
			std::cerr << "Can't find any reference to symbol " << set.identifier;
			std::cerr << " in current context" << std::endl;
			return false;
		}

		bool valid = set.bounded.is_valid(a, u, local);
		if (!valid)
			return valid;

		boost::optional<typeId> t = u.typeOf(a, set.bounded, local);
		if (!t) {
			std::cerr << "Can't compute the type of " << set.bounded << std::endl;
			return false;
		}

		if (s->t != *t) {
			std::cerr << "type of " << set.identifier << " and ";
			std::cerr << set.bounded << " mismatches " << std::endl;
			return false;
		}

		return true;
	}
};



struct validate_recipe_expression
{
	bool &b;
	const ability& a;
	const universe& u;
	symbolList& local;

	validate_recipe_expression(bool & b_, const ability& a_, const universe& u_, 
							   symbolList& local_) :
		b(b_), a(a_), u(u_), local(local_) {}

	void operator() (const recipe_expression& e) const
	{
		b = boost::apply_visitor(validate_recipe_expression_(a, u, local), e.expr) && b;
	}
};

template <typename Iterator> 
bool are_valid_expressions(Iterator begin, Iterator end,
						  const ability& a, const universe& u)
{
	bool b = true;
	std::for_each(begin, end, validate_expression(b, a, u));

	return b;
}

template <typename Iterator>
bool are_valid_recipe_expressions(Iterator begin, Iterator end,
		const ability& a, const universe& u, symbolList& local)
{
	bool b = true;
	std::for_each(begin, end, validate_recipe_expression(b, a, u, local));

	return b;
}

struct dump_unify_pair {
	std::ostream& os;
	std::string separator_;
	const ability & a;
	const universe& u;

	dump_unify_pair(std::ostream& os, const ability& a, const universe& u) : 
		os(os), a(a), u(u) 
	{
		separator_ = "\n" + times(5, "\t");
	}

	void operator() (const unification_expression& p) 
	{
		os << "(" << quoted_string(generate_logic_expression(p.first.expr, a, u));
		os << ", " << quoted_string(generate_logic_expression(p.second.expr, a, u));
		os << ")";
	};

	void separator () { 
		os << separator_;
	}
};

struct dump_recipe_visitor : public boost::static_visitor<std::string>
{
	const universe& u;
	const ability &a;
	const task& t;
	const symbolList& syms;
	mutable size_t counter;
	mutable boost::optional<std::string> target;

	dump_recipe_visitor(const universe & u_, const ability& a_, const task& t_, 
					   const symbolList& syms_) : 
		u(u_), a(a_), t(t_), syms(syms_), counter(0), target(boost::none) {}

	template <typename T> 
	std::string operator() (const T&) const { return ""; }

	std::string local_symbol(const std::string& s) const 
	{
		std::ostringstream oss;
		oss << "boost::fusion::at_c<" << symbolList_index(syms, s) << ">(local_vars)";
		return oss.str();
	}

	std::string unused_symbol(const std::string& t) const
	{
		return "boost::fusion::at_key<" + t + ">(unused_res)";
	}

	std::string compute_target(const expression_ast& e) const
	{
		if (target) {
			std::pair<bool, symbolACL> p = a.get_symbol(*target);
			if (p.first) 
				return "a." + *target;
			else 
				return local_symbol(*target);
		} else {
			boost::optional<typeId> tid = u.typeOf(a, e, syms);
			type t = u.types().get(*tid);
			return unused_symbol(t.type_name());
		}
	}

	std::string compute_make_target() const
	{
		if (target) 
			return local_symbol(*target);
		else 
			return unused_symbol("bool");
	}

	std::string compute_ensure_target() const
	{
		if (target) 
			return local_symbol(*target);
		else 
			return unused_symbol("hyper::model::identifier");
	}

	std::string abortable_function(const std::string& identifier, const expression_ast& e) const
	{
		std::pair<bool, std::string> p = is_tagged_func_call(e, a.name(), u);

		if (p.first) 
			return u.get_extension(p.second).generate_abortable_function(
					u, a, t, symbolList_to_vectorType(syms, u.types()), counter++, identifier);

		std::ostringstream oss;
		std::string indent = times(3, "\t");
		std::string next_indent = "\t" + indent;

		oss << next_indent << "new hyper::model::abortable_function(\n";
		oss << next_indent << "boost::bind(&hyper::model::compute_expression<expression_" << counter << ">::async_eval<\n";
		oss << next_indent << "hyper::model::abortable_computation::cb_type,\n";
		oss << next_indent << "hyper::" << a.name() << "::ability,\n";
		oss << next_indent << symbolList_to_vectorType(syms, u.types()) << " >,\n";
		oss << next_indent << "&expression_exec" << counter++ << ", _1, boost::cref(a), boost::cref(local_vars),\n";
		oss << next_indent << "boost::ref(" << identifier << ")))\n";

		return oss.str();
	}

	std::string operator() (const let_decl& s) const
	{
		target = s.identifier;
		return boost::apply_visitor(*this, s.bounded.expr);
	}

	std::string dump_unification(const std::vector<unification_expression>& unify_list) const
	{
		std::ostringstream oss;
		if (unify_list.empty()) 
			oss << "network::request_constraint::unification_list()";
		else {
			oss << "boost::assign::list_of<std::pair<std::string, std::string> >";
			oss << "\n" << times(5, "\t");
			hyper::compiler::list_of(unify_list.begin(), unify_list.end(), dump_unify_pair(oss, a, u));
			oss << "\n";
		}

		return oss.str();
	}

	std::string operator() (const wait_decl& w) const
	{
		std::string indent = times(3, "\t");
		std::string identifier = compute_target(w.content);
		std::string next_indent = "\t" + indent;

		std::ostringstream oss;
		oss << indent << "push_back(new hyper::model::compute_wait_expression(a.io_s, boost::posix_time::milliseconds(50), \n";
		oss << abortable_function(identifier, w.content) << ",";
		oss << next_indent << identifier << "));\n";

		target = boost::none;

		return oss.str();
	}

	std::string operator() (const expression_ast& e) const
	{
		std::string indent = times(3, "\t");
		std::string identifier = compute_target(e);

		std::ostringstream oss;
		oss << indent << "push_back(" << abortable_function(identifier, e) << indent << ");\n";

		target = boost::none;

		return oss.str();
	}

	std::string operator() (const set_decl& s) const
	{
		std::string indent = times(3, "\t");

		target = s.identifier;
		std::string identifier = compute_target(s.bounded.expr);

		std::ostringstream oss;
		oss << indent << "push_back(" << abortable_function(identifier, s.bounded.expr) << indent << ");\n";
		target = boost::none;

		return oss.str();
	}

	std::string operator() (const recipe_op<MAKE>& r) const
	{
		std::string indent = times(3, "\t");
		std::string indent_next = times(4, "\t");
		std::string identifier = compute_make_target();
		std::ostringstream oss;

		oss << indent << "push_back(new hyper::model::compute_make_expression(a, ";
		oss << quoted_string(*(r.content[0].dst)) << ", ";
		oss << quoted_string(generate_logic_expression(r.content[0].logic_expr.main, a, u));
		oss << ", \n" << indent_next << dump_unification(r.content[0].logic_expr.unification_clauses);
		oss << ", " << identifier << "));\n";
		target = boost::none;

		return oss.str();

	}

	std::string operator() (const recipe_op<ENSURE>& r) const
	{
		std::string indent = times(3, "\t");
		std::string indent_next = times(4, "\t");
		std::string identifier = compute_ensure_target();
		std::ostringstream oss;

		oss << indent << "push_back(new hyper::model::compute_ensure_expression(a, ";
		oss << quoted_string(*(r.content[0].dst)) << ", \n" << indent_next;
		oss << quoted_string(generate_logic_expression(r.content[0].logic_expr.main, a, u));
		oss << ", \n" << indent_next << dump_unification(r.content[0].logic_expr.unification_clauses);
		oss << ", \n" << indent_next << identifier << "));\n";
		target = boost::none;

		return oss.str();
	}

	std::string operator() (const abort_decl& a) const
	{
		std::string indent = times(3, "\t");
		std::string indent_next = times(4, "\t");
		std::ostringstream oss;

		oss << indent << "push_back(new hyper::model::compute_abort_expression(a, \n";
		oss << indent_next << local_symbol(a.identifier) << ", \n";
		oss << indent_next << unused_symbol("boost::mpl::void_") << "));";
		target = boost::none;

		return oss.str();
	}
};

struct dump_recipe_expression {
	std::ostream& oss;
	const universe &u;
	const ability &a;
	const task& t;
	const symbolList& syms;
	dump_recipe_visitor vis;

	dump_recipe_expression(std::ostream& oss_, const universe& u_,
						   const ability & a_, const task& t_,
						   const symbolList& syms_) : 
		oss(oss_), u(u_), a(a_), t(t_), syms(syms_),
		vis(u, a, t, syms) {}

	void operator() (const recipe_expression& r)
	{
		oss << boost::apply_visitor(vis, r.expr);
	}
};

struct extract_expression_visitor : public boost::static_visitor<void>
{
	std::vector<expression_ast>& list;

	extract_expression_visitor(std::vector<expression_ast>& list) : 
		list(list) {}

	template <typename T> 
	void operator() (const T&) const {}

	void operator() (const let_decl& l) const
	{
		boost::apply_visitor(extract_expression_visitor(list), l.bounded.expr);
	}

	void operator() (const expression_ast& e) const 
	{
		list.push_back(e);
	}

	void operator() (const set_decl& s) const
	{
		list.push_back(s.bounded);
	}

	void operator() (const wait_decl& w) const
	{
		list.push_back(w.content);
	}
};

struct extract_expression {
	std::vector<expression_ast>& list;

	extract_expression(std::vector<expression_ast>& list) : list(list) {}

	void operator() (const recipe_expression& r)
	{
		boost::apply_visitor(extract_expression_visitor(list), r.expr);
	}
};

struct extract_unused_result_visitor : public boost::static_visitor<void>
{
	std::set<std::string>& list;
	const universe& u;
	const ability& a;
	const symbolList& syms;
	mutable bool catched;

	extract_unused_result_visitor(std::set<std::string>& list, const universe& u,
								 const ability& a, const symbolList& syms) :
		list(list), u(u), a(a), syms(syms), catched(false)
	{}

	/* set_decl, abort does not return anything */
	template <typename T>
	void operator() (const T&) const {}

	void operator() (const let_decl& l) const
	{
		catched = true;
		boost::apply_visitor(*this, l.bounded.expr);
	}

	void operator() (const expression_ast& e) const
	{
		if (!catched) {
			boost::optional<typeId> tid = u.typeOf(a, e, syms); 
			type t = u.types().get(*tid);
			list.insert(t.type_name());
		}
	}

	void operator() (const wait_decl&) const
	{
		if (!catched) 
			list.insert("bool");
	}

	void operator() (const recipe_op<MAKE>&) const
	{
		if (!catched)
			list.insert("bool");
	}

	void operator() (const recipe_op<ENSURE>&) const
	{
		if (!catched)
			list.insert("hyper::model::identifier");
	}

	void operator() (const abort_decl&) const
	{
		if(!catched) 
			list.insert("boost::mpl::void_");
	}
};

struct extract_unused_result {
	std::set<std::string>& list;
	const universe& u;
	const ability& a;
	const symbolList& syms;

	extract_unused_result(std::set<std::string>& list, const universe& u, 
						  const ability& a, const symbolList& syms) :
		list(list), u(u), a(a), syms(syms)
	{}

	void operator() (const recipe_expression& r)
	{
		boost::apply_visitor(extract_unused_result_visitor(list, u, a, syms), r.expr);
	}
};

std::string unused_results(const std::set<std::string>& s)
{
	if (s.empty()) return "";

	std::set<std::string>::const_iterator it = s.begin();
	std::ostringstream oss;
	oss << "boost::fusion::set<";
	while (it != s.end()) {
		oss << *it++;
		if (it != s.end())
			oss << ", ";
	}
	oss << "> unused_res;";

	return oss.str();
}

struct is_tagged_func_call_helper : public boost::static_visitor< std::pair<bool, std::string> >
{
	std::string name;
	const universe& u;
	
	is_tagged_func_call_helper(const std::string name, const universe& u) : 
		name(name), u(u) {}

	template <typename T>
	std::pair<bool, std::string> operator() (const T&) const
	{
		return std::make_pair(false, "");
	}

	std::pair<bool, std::string> operator() (const function_call &f) const
	{
		// add scope to do the search
		std::string func_name = scope::add_scope(name, f.fName);
		std::pair<bool, functionDef> p = u.get_functionDef(func_name);
		if (p.first == false) {
			std::cerr << "Unknow function " << f.fName << std::endl;
			return std::make_pair(false, "");
		}

		const boost::optional<std::string>& tag = p.second.tag();
		if (tag)
			return std::make_pair(true, *tag);

		return std::make_pair(false, "");
	}
};

std::pair<bool, std::string> 
is_tagged_func_call(const expression_ast& e, const std::string& name, const universe &u)
{
	return boost::apply_visitor(is_tagged_func_call_helper(name, u), e.expr);
}

struct dump_eval_expression {
	std::ostream& oss;
	const universe &u;
	const ability &a;
	const task& t;
	std::string local_data;
	size_t counter;
	const symbolList& local_symbol;

	dump_eval_expression(std::ostream& oss_, const universe& u_,
						   const ability & a_, const task& t_, 
						   const std::string& local_data,
						   const symbolList& local_symbol) : 
		oss(oss_), u(u_), a(a_), t(t_), local_data(local_data), 
		counter(0), local_symbol(local_symbol) 
	{}

	void operator() (const expression_ast& e)
	{
		std::pair<bool, std::string> p = is_tagged_func_call(e, a.name(), u);
		if (p.first)
			return u.get_extension(p.second).dump_eval_expression
				(oss, u, a, t, local_data, local_symbol, counter++, e);

		std::string indent = "\t\t";
		std::string next_indent = "\t" + indent;
		oss << indent << "struct expression_" << counter++ << " {\n";

		/* Compute the necessary arguments */
		extract_symbols syms(a);
		syms.extract(e);
		oss << next_indent << "typedef model::update_variables< ";
		oss << "hyper::" << a.name() << "::ability, ";
		oss << syms.local_with_updater.size() << ", ";
		oss << syms.remote_vector_type_output(u) << " > updater_type;\n";

		/* Compute the return type of the expression */
		const typeList& tList = u.types();
		boost::optional<typeId> id = u.typeOf(a, e, local_symbol); 
		assert(id);
		type t = tList.get(*id);
		oss << next_indent << "typedef " << t.type_name() << " return_type;\n\n";
		oss << next_indent << "return_type operator() (const updater_type& updater, ";
		oss << "const hyper::" << a.name() << "::ability& a,\n";
		oss << next_indent << "\t\t\t\t\t\tconst " << local_data << " & local_vars)\n";
		oss << next_indent << "{\n";
		oss << next_indent << "\treturn " << expression_ast_output(e, syms.remote, local_symbol) << ";\n";
		oss << next_indent << "}\n";
		oss << indent << "};" << std::endl;
	}
};

struct dump_tag_depends
{
	std::ostream& oss;
	const universe& u;
	std::string name;

	dump_tag_depends(std::ostream& oss,
					 const universe& u,
					 const std::string& name) : 
		oss(oss), u(u), name(name) {}

	void operator() (const std::string& tag) {
		oss << "#include <" << name << "/" << tag << "_funcs.hh>\n";
		u.get_extension(tag).recipe_additional_includes(oss, name);
	}
};

struct dump_required_agents
{
	std::ostream& oss;
	std::string name;

	dump_required_agents(std::ostream& oss, const std::string& name) : 
		oss(oss), name(name) {}

	void operator() (const std::string& agent) 
	{
		/* Don't requiere ourself, we are already here ! */
		if (agent == name)
			return;
		oss << times(3, "\t") << "required_agents.insert(";
		oss << quoted_string(agent) << ");\n";
	}
};

namespace hyper {
	namespace compiler {
		recipe::recipe(const recipe_decl& r_parser, const ability& a, 
						const task& t, const typeList& tList_) :
			name(r_parser.name), pre(r_parser.conds.pre.list),
			post(r_parser.conds.post.list), body(r_parser.body.list),
			context_a(a), context_t(t), tList(tList_), local_symbol(tList) 
		{}

		bool recipe::validate(const universe& u) 
							  
		{
			return are_valid_expressions(pre.begin(), pre.end(), context_a, u) &&
				   are_valid_expressions(post.begin(), post.end(), context_a, u) &&
				   are_valid_recipe_expressions(body.begin(), body.end(), context_a, u, local_symbol);
		}

		void recipe::dump_include(std::ostream& oss, const universe& u) const
		{
			const std::string indent="\t\t";
			const std::string next_indent = indent + "\t";

			std::string exported_name_big = exported_name();
			std::transform(exported_name_big.begin(), exported_name_big.end(), 
						   exported_name_big.begin(), toupper);
			guards g(oss, context_a.name(), exported_name_big + "_HH");

			extract_symbols pre_symbols(context_a);
			std::for_each(pre.begin(), pre.end(), 
						  boost::bind(&extract_symbols::extract, &pre_symbols, _1));


			oss << "#include <model/recipe.hh>" << std::endl;
			oss << "#include <model/evaluate_conditions.hh>" << std::endl;

			namespaces n(oss, context_a.name());
			oss << indent << "struct ability;" << std::endl;
			oss << indent << "struct " << exported_name();
			oss << " : public model::recipe {" << std::endl;

			if (!pre.empty()) {
				oss << next_indent << "typedef hyper::model::evaluate_conditions<";
				oss << pre.size() << ", ability, " << pre_symbols.local_with_updater.size() << ", ";
				oss << pre_symbols.remote_vector_type_output(u);
				oss << " > pre_conditions;" << std::endl; 
				oss << next_indent << "pre_conditions preds;" << std::endl;
			}

			oss << next_indent << "ability &a;" << std::endl;
			

			oss << next_indent << exported_name();
			oss << "(ability& a_);" << std::endl; 
			oss << next_indent;
			oss << "void async_evaluate_preconditions(model::condition_execution_callback cb);";
			oss << std::endl;
			oss << next_indent << "void do_execute(model::abortable_computation::cb_type cb);\n"; 

			oss << indent << "};" << std::endl;
		}

		void recipe::add_depends(depends& deps, const universe& u) const 
		{
			void (*f1)(const expression_ast&, const std::string&, const universe&, depends&) = &hyper::compiler::add_depends;
			void (*f2)(const recipe_expression&, const std::string&, const universe&, depends&) = &hyper::compiler::add_depends;
			std::for_each(pre.begin(), pre.end(),
						  boost::bind(f1 ,_1, boost::cref(context_a.name()),
											  boost::cref(u),
											  boost::ref(deps)));
			std::for_each(body.begin(), body.end(),
						  boost::bind(f2 ,_1, boost::cref(context_a.name()),
											  boost::cref(u),
											  boost::ref(deps)));
		}

		void recipe::dump(std::ostream& oss, const universe& u) const
		{
			const std::string indent="\t\t";
			const std::string next_indent = indent + "\t";
			const std::string next_next_indent = next_indent + "\t";

			depends deps;
			add_depends(deps, u);
			
			oss << "#include <" << context_a.name() << "/ability.hh>\n"; 
			oss << "#include <" << context_a.name();
			oss << "/recipes/" << name << ".hh>\n"; 
			oss << "#include <model/abortable_function.hh>\n";
			oss << "#include <model/compute_abort_expression.hh>\n";
			oss << "#include <model/compute_ensure_expression.hh>\n";
			oss << "#include <model/compute_expression.hh>\n";
			oss << "#include <model/compute_make_expression.hh>\n";
			oss << "#include <model/compute_wait_expression.hh>\n";
			oss << "#include <boost/assign/list_of.hpp>\n"; 
			oss << "#include <boost/fusion/container/vector.hpp>\n";
			oss << "#include <boost/fusion/container/set.hpp>\n";
			oss << "#include <boost/fusion/include/at_key.hpp>\n" << std::endl;


			std::for_each(deps.fun_depends.begin(), 
						  deps.fun_depends.end(), dump_depends(oss, "import.hh"));
			oss << "\n";

			std::for_each(deps.tag_depends.begin(),
						  deps.tag_depends.end(), dump_tag_depends(oss, u, context_a.name()));
			oss << "\n\n";

			extract_symbols pre_symbols(context_a);
			std::for_each(pre.begin(), pre.end(), 
						  boost::bind(&extract_symbols::extract, &pre_symbols, _1));
			{
			anonymous_namespaces n(oss);
			oss << indent << "using namespace hyper;\n";
			oss << indent << "using namespace hyper::" << context_a.name() << ";\n";

			std::string context_name = "hyper::" + context_a.name() + "::" + exported_name();
			exec_expression_output e_dump(context_a, context_name, oss, "pre_", 
										  pre_symbols.remote, symbolList(u.types()));
			std::for_each(pre.begin(), pre.end(), e_dump);
			} 

			/* Extract expression_ast to execute from the body */
			std::vector<expression_ast> expression_list;
			std::for_each(body.begin(), body.end(), extract_expression(expression_list));

			/* Get the list of instruction executed but not catched by a set or a let */
			std::set<std::string> unused_result_set;
			std::for_each(body.begin(), body.end(), extract_unused_result(unused_result_set, u, context_a, local_symbol));
			{ 
				anonymous_namespaces n(oss);
				oss << indent << "using namespace hyper;\n";
				oss << indent << "using namespace hyper::" << context_a.name() << ";\n";

				std::string local_data = symbolList_to_vectorType(local_symbol, u.types());

				dump_eval_expression e_dump(oss, u, context_a, context_t, local_data, local_symbol);
				std::for_each(expression_list.begin(), expression_list.end(), e_dump);

				oss << indent << "struct exec_driver : public hyper::model::abortable_computation {\n";
				oss << indent << "\tability& a;\n";
				oss << indent << "\t" << local_data << " local_vars;\n";
				oss << indent << "\t" << unused_results(unused_result_set) << "\n";
				for (size_t i = 0; i < expression_list.size(); ++i) {
					std::pair<bool, std::string> p;
					p = is_tagged_func_call(expression_list[i], context_a.name(), u);
					if (!p.first) {
						oss << indent << "\texpression_" << i << "::updater_type updater" << i << ";\n";
						oss << indent << "\thyper::model::compute_expression<expression_" << i ;
						oss << "> expression_exec" << i << ";\n";
					} else {
						u.get_extension(p.second).generate_expression_caller(oss, i);
					}
				}
				oss << indent << "\texec_driver(ability &a) : a(a)";
				for (size_t i = 0; i < expression_list.size(); ++i) {
					extract_symbols syms(context_a);
					syms.extract(expression_list[i]);
					if (syms.empty()) {
						oss << ",\n";
					} else {
						oss << ",\n" << indent << "\t\tupdater" << i << "(a";
						if (!syms.local_with_updater.empty())
							oss << ",\n\t" << syms.local_list_variables_updated(next_indent);
						if (!syms.remote.empty())
							oss << ",\n\t" << syms.remote_list_variables(next_indent);
						oss << indent << "\t\t),\n";
					}
					oss << indent << "\t\texpression_exec" << i << "(updater" << i << ")";
				}
				oss << "\n" << indent << "\t{\n";
				std::for_each(body.begin(), body.end(), 
							  dump_recipe_expression(oss, u, context_a, context_t, local_symbol));
				oss << indent << "\t};\n";
				oss << indent << "};";
			}

			namespaces n(oss, context_a.name());

			/* Generate constructor */
			oss << indent << exported_name() << "::" << exported_name();
			oss << "(hyper::" << context_a.name() << "::ability & a_) :" ;
			oss << "model::recipe(" << quoted_string(name);
			oss << ", a_), a(a_)";
			if (!pre.empty()) {
			oss << ",\n";
			oss << indent << "preds(a_, \n";
			oss << next_indent << "boost::assign::list_of<hyper::" << context_a.name(); 
			oss << "::" << exported_name() << "::pre_conditions::condition>\n";
			generate_condition e_cond(oss, "pre", context_a, u);
			std::for_each(pre.begin(), pre.end(), e_cond);

			std::string base_type = "pre_conditions::remote_values::remote_vars_conf";

			if (!pre_symbols.local_with_updater.empty())
				oss << ", " << pre_symbols.local_list_variables_updated(next_next_indent);
			if (!pre_symbols.remote.empty()) {
				oss << next_indent << ", " << base_type << "(\n";
				oss << pre_symbols.remote_list_variables(next_next_indent);
				oss << next_indent << ")\n";
			}
			oss << indent << ")" << std::endl;
			}
			oss << indent << "{" << std::endl;
			std::for_each(deps.var_depends.begin(), deps.var_depends.end(),
						  dump_required_agents(oss, context_a.name()));
			oss << indent << "}\n\n";

			oss << indent << "void " << exported_name();
			oss << "::async_evaluate_preconditions(model::condition_execution_callback cb)";
			oss << std::endl;
			oss << indent << "{" << std::endl;
			if (pre.empty()) 
				oss << next_indent << "cb(boost::system::error_code(), hyper::model::conditionV());\n";
			else
				oss << next_indent << "preds.async_compute(cb);\n"; 
			oss << indent << "}" << std::endl;

			oss << indent << "void " << exported_name();
			oss << "::do_execute(hyper::model::abortable_computation::cb_type cb)\n";
			oss << indent << "{\n";
			oss << next_indent << "computation = new exec_driver(a);\n";
			oss << next_indent << "computation->compute(cb);\n";
			oss << indent << "}\n";
			oss << std::endl;
		}
	}
}
