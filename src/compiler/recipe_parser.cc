
#include <compiler/parser.hh>
#include <compiler/cond_block_parser.hh>
#include <compiler/recipe_parser.hh>
#include <compiler/import_parser.hh>
#include <compiler/utils.hh>

#include <boost/fusion/adapted/std_pair.hpp>
#include <boost/fusion/include/std_pair.hpp>

using namespace hyper::compiler;

namespace qi = boost::spirit::qi;
namespace ascii = boost::spirit::ascii;
namespace fusion = boost::fusion;
namespace phoenix = boost::phoenix;

//#define HYPER_DEBUG_RULES


std::ostream& hyper::compiler::operator<< (std::ostream& os, const body_block_decl& d)
{
	std::copy(d.list.begin(), d.list.end(), std::ostream_iterator<recipe_expression>( os ));
	return os;
}

std::ostream& hyper::compiler::operator<< (std::ostream& os, const recipe_decl& r)
{
	os << r.name;
	os << r.conds;
	os << r.body;
	return os;
}

std::ostream& hyper::compiler::operator<< (std::ostream& os, const recipe_decl_list & l)
{
	std::copy(l.recipes.begin(), l.recipes.end(), std::ostream_iterator<recipe_decl>( os ));
	return os;
}

std::ostream& hyper::compiler::operator<< (std::ostream& os, const letname_expression_decl& l)
{
	os << l.name << " ==> " << l.expr;
	return os;
}

std::ostream& hyper::compiler::operator<< (std::ostream& os, const recipe_context_decl& decl)
{
	std::copy(decl.expression_names.begin(), decl.expression_names.end(), 
			  std::ostream_iterator<letname_expression_decl>(os, ", "));
}

struct make_pair {
	std::pair<std::string, expression_ast>
	operator() (const letname_expression_decl& decl) 
	{
		return std::make_pair(decl.name, decl.expr);
	}
};

recipe_context_decl::map_type 
hyper::compiler::make_name_expression_map(const recipe_context_decl& decl)
{
	recipe_context_decl::map_type res;
	std::transform(decl.expression_names.begin(), decl.expression_names.end(),
				   std::inserter(res, res.end()), 
				   make_pair());

	return res;
}


BOOST_FUSION_ADAPT_STRUCT(
	body_block_decl,
	(std::vector<recipe_expression>, list)
)

BOOST_FUSION_ADAPT_STRUCT(
	recipe_decl,
	(std::string, name)
	(cond_block_decl, conds)
	(body_block_decl, body)
)

BOOST_FUSION_ADAPT_STRUCT(
	logic_expression_decl,
	(expression_ast, main)
	(std::vector<unification_expression>, unification_clauses)

);
BOOST_FUSION_ADAPT_STRUCT(
	recipe_decl_list,
	(recipe_context_decl, context)
	(std::vector<recipe_decl>, recipes)
)

BOOST_FUSION_ADAPT_STRUCT(
	let_decl,
	(std::string, identifier)
	(recipe_expression, bounded)
)

BOOST_FUSION_ADAPT_STRUCT(
	abort_decl,
	(std::string, identifier)
)

BOOST_FUSION_ADAPT_STRUCT(
	set_decl,
	(std::string, identifier)
	(expression_ast, bounded)
)	

BOOST_FUSION_ADAPT_STRUCT(
	wait_decl,
	(expression_ast, content)
)


BOOST_FUSION_ADAPT_STRUCT(
	letname_expression_decl,
	(std::string, name)
	(expression_ast, expr)
)

BOOST_FUSION_ADAPT_STRUCT(
	recipe_context_decl,
	(std::vector<letname_expression_decl>, expression_names)
)

template <typename Iterator>
struct recipe_context_grammar :
	qi::grammar<Iterator, recipe_context_decl(), white_space<Iterator> >
{
	typedef white_space<Iterator> white_space_;
	recipe_context_grammar() : recipe_context_grammar::base_type(start)
	{
		using qi::lit;

		start = letname_list;

		letname_list = (*letname_expression_);

		letname_expression_ = lit("letname") 
							  > identifier
							  > expression
							  ;

		start.name("recipe context");
		letname_expression_.name("name context");
		letname_list.name("name context list");
#ifdef HYPER_DEBUG_RULES
		debug(start);
		debug(letname_list);
		debug(letname_expression_);
#endif
	}

	qi::rule<Iterator, letname_expression_decl(), white_space_> letname_expression_;
	qi::rule<Iterator, std::vector<letname_expression_decl>(), white_space_> letname_list;
	qi::rule<Iterator, recipe_context_decl(), white_space_> start;

	grammar_expression<Iterator> expression;
	identifier_grammar<Iterator> identifier;
};

template <typename Iterator>
struct logic_expression_grammar :
	qi::grammar<Iterator, logic_expression_decl(), white_space<Iterator> >
{
	typedef white_space<Iterator> white_space_;
	logic_expression_grammar() :
		logic_expression_grammar::base_type(start)
	{
		using qi::lit;

		start = expression 
			   > -(lit("where")
				    > unification_list
				   )
			;

		unification_list = unification_expression_ % lit("&&");

		unification_expression_ =  atom 
							  > lit("==") 
							  > atom
							  ;

		start.name("logic_expression");
		unification_list.name("unification list");
		unification_expression_.name("unification expression");

#ifdef HYPER_DEBUG_RULES
		debug(start);
		debug(unification_list);
		debug(unification_expression_);
#endif
	}

	qi::rule<Iterator, unification_expression(), white_space_> unification_expression_;
	qi::rule<Iterator, std::vector<unification_expression>(), white_space_> unification_list;
	qi::rule<Iterator, logic_expression_decl(), white_space_> start;

	atom_grammar<Iterator> atom;
	grammar_expression<Iterator> expression;
};

template <typename Iterator>
struct body_block_grammar :
	qi::grammar<Iterator, body_block_decl(), white_space<Iterator> >
{
	typedef white_space<Iterator> white_space_;

	body_block_grammar() :
		body_block_grammar::base_type(start)
	{
		using qi::lit;

		start = lit("body")
			>> lit("=")
			>> lit('{')
			>> body_decl_list
			>> lit('}')
			>> -lit(';')
			;

		body_decl_list = (*body_decl	);

		body_decl = (	let_decl_	
					|	make_decl_
					|   ensure_decl_
					|   wait_decl_
					|	abort_decl_
					|	set_decl_
					|	expression
					)
					> -lit(';')
					;

		/* 
		 * XXX not the right grammar here, it is probably only right for ensure
		 * and expression
		 */
		let_decl_ = lit("let")
				 > identifier
				 > body_decl
				 ;

		make_decl_ = lit("make")
				  > lit("(")
				  > expression_list
				  > lit(")")
				  ;

		set_decl_ = lit("set")
				 > scoped_identifier
				 > expression
				 ;

		ensure_decl_ = lit("ensure")
				  > lit("(")
				  > expression_list
				  > lit(")")
				  ;

		abort_decl_ = lit("abort")
				   > identifier
				   ;

		wait_decl_ = lit("wait")
				  > lit("(")
				  > expression
				  > lit(")")
				  ;

		expression_list = logic_expression % lit("=>>");

		start.name("body block");
		body_decl_list.name("recipe expression list");
		body_decl.name("recipe expression");
		expression_list.name("expression list");
		let_decl_.name("let declaration");
		set_decl_.name("set declaration");
		abort_decl_.name("abort declaration");
		make_decl_.name("make declaration");
		ensure_decl_.name("ensure declaration");
		wait_decl_.name("wait declaration");

#ifdef HYPER_DEBUG_RULES
		debug(start);
		debug(body_decl_list);
		debug(body_decl);
		debug(expression_list);
		debug(let_decl_);
		debug(set_decl_);
		debug(abort_decl_);
		debug(make_decl_);
		debug(ensure_decl_);
		debug(wait_decl_);
#endif
	}

	qi::rule<Iterator, body_block_decl(), white_space_> start;
	qi::rule<Iterator, std::vector<recipe_expression>(), white_space_> body_decl_list;
	qi::rule<Iterator, recipe_expression(), white_space_> body_decl;
	qi::rule<Iterator, std::vector<logic_expression_decl>(), white_space_> expression_list;
	qi::rule<Iterator, let_decl(), white_space_> let_decl_;
	qi::rule<Iterator, set_decl(), white_space_> set_decl_;
	qi::rule<Iterator, abort_decl(), white_space_> abort_decl_;
	qi::rule<Iterator, recipe_op<MAKE>(), white_space_> make_decl_;
	qi::rule<Iterator, recipe_op<ENSURE>(), white_space_> ensure_decl_;
	qi::rule<Iterator, wait_decl(), white_space_> wait_decl_;

	grammar_expression<Iterator> expression;
	identifier_grammar<Iterator> identifier;
	logic_expression_grammar<Iterator> logic_expression;
	scoped_identifier_grammar<Iterator> scoped_identifier;
};

template <typename Iterator>
struct  grammar_recipe : 
	qi::grammar<Iterator, recipe_decl_list(), white_space<Iterator> >
{
    typedef white_space<Iterator> white_space_;

    grammar_recipe(hyper::compiler::parser &p) : 
		grammar_recipe::base_type(start),
		import_list(p)
	{
	    using qi::lit;
        using namespace qi::labels;

		start = import_list > recipe_context_ > recipe_list;

		recipe_list = (*recipe);

		recipe = scoped_identifier
			 > lit('=')
			 > lit("recipe")
			 > lit("{")
			 > cond_block
			 > body_block
			 > lit('}')
			 > -lit(';')
			 ;

		start.name("recipe list");
		recipe_list.name("recipe list helper");
		recipe.name("recipe");

#ifdef HYPER_DEBUG_RULES
		debug(start);
		debug(recipe);
		debug(recipe_list);
#endif

		qi::on_error<qi::fail> (start, error_handler(_4, _3, _2));
	}

	qi::rule<Iterator, recipe_decl_list(), white_space_> start;
	qi::rule<Iterator, std::vector<recipe_decl>(), white_space_> recipe_list;
	qi::rule<Iterator, recipe_decl(), white_space_> recipe;

	scoped_identifier_grammar<Iterator> scoped_identifier;
	cond_block_grammar<Iterator> cond_block;
	body_block_grammar<Iterator> body_block;
	import_grammar<Iterator> import_list;
	recipe_context_grammar<Iterator> recipe_context_;
};

bool parser::parse_logic_expression(const std::string& expr)
{
	logic_expression_decl decl;
	logic_expression_grammar<std::string::const_iterator> g;
	bool r;
	try {
		r = parse(g, expr, decl);
	} catch (hyper::compiler::import_exception &e) 
	{
		std::cerr << e.what() << std::endl;
		return false;
	}
	return r;
}

bool parser::parse_recipe(const std::string& expr)
{
	recipe_decl_list result;
	return parse_recipe(expr, result);
}

bool parser::parse_recipe(const std::string& expr, recipe_decl_list& res)
{
	grammar_recipe<std::string::const_iterator> g(*this);
	bool r;
	try {
		r = parse(g, expr, res);
	} catch (hyper::compiler::import_exception &e) 
	{
		std::cerr << e.what() << std::endl;
		return false;
	}
	return r;
}

bool parser::parse_recipe_file(const std::string& fileName)
{
	std::string content = read_from_file(fileName);
	return parse_recipe(content);
}

bool parser::parse_recipe_file(const std::string& fileName, recipe_decl_list& res)
{
	std::string content = read_from_file(fileName);
	return parse_recipe(content, res);
}
