
#ifndef _HYPER_EXPRESSION_HH_
#define _HYPER_EXPRESSION_HH_

#include <vector>

#include <boost/variant.hpp>

namespace hyper {
	namespace compiler {
		template <typename T>
		struct Constant
		{
			T value;
		};

		struct empty {};
		struct function_call;
		struct binary_op;
		struct unary_op;

		struct expression_ast
		{
			typedef
				boost::variant<
				  empty								// can't happen
				, Constant<int>						// constant of type int
				, Constant<double>					// constant of type double
				, Constant<std::string>				// constant of type string
				, Constant<bool>					// constant of type bool
				, std::string						// variable identifier
				, boost::recursive_wrapper<function_call>
				, boost::recursive_wrapper<expression_ast>
				, boost::recursive_wrapper<binary_op>
				, boost::recursive_wrapper<unary_op>
				>
				type;

			expression_ast()
				: expr(empty()) {}

			template <typename Expr>
				expression_ast(Expr const& expr)
				: expr(expr) {}

			type expr;

			// XXX Any intelligent way to factorize this stuff ...
			expression_ast& add(const expression_ast& right);
			expression_ast& sub(const expression_ast& right);
			expression_ast& mult(const expression_ast& right);
			expression_ast& div(const expression_ast& right);
			expression_ast& eq(const expression_ast& right);
			expression_ast& neq(const expression_ast& right);
			expression_ast& lt(const expression_ast& right);
			expression_ast& lte(const expression_ast& right);
			expression_ast& gt(const expression_ast& right);
			expression_ast& gte(const expression_ast& right);
			expression_ast& logical_and(const expression_ast& right);
			expression_ast& logical_or(const expression_ast& right);

			expression_ast& neg(const expression_ast& subject);
		};

		std::ostream& operator << (std::ostream& os, const expression_ast& e);

		enum binary_op_kind { ADD, SUB, MUL, DIV , EQ, NEQ, LT, LTE, GT, GTE, AND, OR };

		std::ostream& operator << (std::ostream& os, const binary_op_kind& k);
			
		struct binary_op
		{
			binary_op(
					binary_op_kind op
					, expression_ast const& left
					, expression_ast const& right)
				: op(op), left(left), right(right) {}

			binary_op_kind op;
			expression_ast left;
			expression_ast right;
		};

		enum unary_op_kind { PLUS, NEG };

		std::ostream& operator << (std::ostream& os, const unary_op_kind& k);

		struct unary_op
		{
			unary_op(
					unary_op_kind op
					, expression_ast const& subject)
				: op(op), subject(subject) {}

			unary_op_kind op;
			expression_ast subject;
		};

		struct function_call {
			std::string fName;
			std::vector< expression_ast > args;
		};
	};
};



#endif /* _HYPER_EXPRESSION_HH_ */
