#ifndef HYPER_MODEL_OPERATOR_HH_
#define HYPER_MODEL_OPERATOR_HH_

#include <string>

#include <logic/expression.hh>
#include <logic/function_def.hh>
#include <model/execute_impl.hh>

#include <boost/logic/tribool.hpp>

namespace hyper {
	namespace model {
		
		namespace details {
			template <typename T>
			struct equal {
				typedef bool result_type;
				typedef typename boost::mpl::vector<T, T> args_type;
				static bool apply(const T& t1, const T& t2) 
				{
					return t1 == t2;
				}
			};

			template <typename T>
			struct nequal {
				typedef bool result_type;
				typedef typename boost::mpl::vector<T, T> args_type;
				static bool apply(const T& t1, const T& t2) 
				{
					return !(t1 == t2);
				}
			};

			template <typename T>
			struct add {
				typedef T result_type;
				typedef typename boost::mpl::vector<T, T> args_type;
				static T apply(const T& t1, const T& t2) 
				{
					return (t1 + t2);
				}
			};

			template <typename T>
			struct minus {
				typedef T result_type;
				typedef typename boost::mpl::vector<T, T> args_type;
				static T apply(const T& t1, const T& t2) 
				{
					return (t1 - t2);
				}
			};

			template <typename T>
			struct times {
				typedef T result_type;
				typedef typename boost::mpl::vector<T, T> args_type;
				static T apply(const T& t1, const T& t2) 
				{
					return (t1 * t2);
				}
			};

			template <typename T>
			struct divides {
				typedef T result_type;
				typedef typename boost::mpl::vector<T, T> args_type;
				static T apply(const T& t1, const T& t2) 
				{
					return (t1 / t2);
				}
			};

			template <typename T>
			struct negate {
				typedef T result_type;
				typedef typename boost::mpl::vector<T> args_type;
				static T apply(const T& t1) 
				{
					return -t1;
				}
			};

			template <typename T>
			struct less {
				typedef bool result_type;
				typedef typename boost::mpl::vector<T, T> args_type;
				static bool apply(const T& t1, const T& t2) 
				{
					return (t1 < t2);
				}
			};

			template <typename T>
			struct less_equal {
				typedef bool result_type;
				typedef typename boost::mpl::vector<T, T> args_type;
				static bool apply(const T& t1, const T& t2) 
				{
					return (t1 <= t2);
				}
			};

			template <typename T>
			struct greater {
				typedef bool result_type;
				typedef typename boost::mpl::vector<T, T> args_type;
				static bool apply(const T& t1, const T& t2) 
				{
					return !(t1 <= t2);
				}
			};

			template <typename T>
			struct greater_equal {
				typedef bool result_type;
				typedef typename boost::mpl::vector<T, T> args_type;
				static bool apply(const T& t1, const T& t2) 
				{
					return !(t1 < t2);
				}
			};

			struct logic_less {
				template <typename T>
					static bool apply(const T& t1, const T& t2)
					{
						return t1 < t2;
					}
			};

			struct logic_less_equal {
				template <typename T>
					static bool apply(const T& t1, const T& t2)
					{
						return t1 <= t2;
					}
			};

			struct logic_greater {
				template <typename T>
					static bool apply(const T& t1, const T& t2)
					{
						return !(t1 <= t2);
					}
			};

			struct logic_greater_equal {
				template <typename T>
					static bool apply(const T& t1, const T& t2)
					{
						return !(t1 <= t2);
					}
			};

			template <typename Op>
			struct logic_eval_ : public boost::static_visitor<boost::logic::tribool>
			{
				template <typename T, typename U>
				boost::logic::tribool operator()(const T&, const U&) const
				{
					return boost::logic::indeterminate;
				}

				template <typename U>
				boost::logic::tribool operator () (const hyper::logic::Constant<U>& u, 
												   const hyper::logic::Constant<U>& v) const
				{
					return (Op::template apply<U>(u.value, v.value));
				}
			};

			template <typename Op>
			struct logic_eval 
			{
				boost::logic::tribool operator() (const hyper::logic::expression& e1, 
												  const hyper::logic::expression& e2) const
				{
					return boost::apply_visitor(logic_eval_<Op>(), e1.expr, e2.expr);
				}
			};
		}


		template <typename T>
		void add_equalable_type(model::functions_map& fMap, logic::funcDefList& fList, 
								std::string typeName)
		{
			fMap.add("equal_" + typeName, new function_execution<details::equal<T> >());
			fList.add("equal_" + typeName, 2);
			fMap.add("not_equal_" + typeName, new function_execution<details::nequal<T> > ());
			fList.add("not_equal_" + typeName, 2);
		}

		template <typename T>
		void add_numeric_type(model::functions_map& fMap, logic::funcDefList& fList,
							  std::string typeName)
		{
			fMap.add("add_" + typeName, new function_execution<details::add<T> > ());
			fList.add("add_" + typeName, 2);
			fMap.add("minus_" + typeName, new function_execution<details::minus<T> >());
			fList.add("minus_" + typeName, 2);
			fMap.add("times_" + typeName, new function_execution<details::times<T> >());
			fList.add("times_" + typeName, 2);
			fMap.add("divides_" + typeName, new function_execution<details::divides<T> >());
			fList.add("divides_" + typeName, 2);
			fMap.add("negate_" + typeName, new function_execution<details::negate<T> >());
			fList.add("negate_" + typeName, 1);
		}

		template <typename T>
		void add_comparable_type(model::functions_map& fMap, logic::funcDefList& fList, 
								 std::string typeName)
		{
			fMap.add("less_" + typeName, new function_execution<details::less<T> > ());
			fList.add("less_" + typeName, 2);
			fMap.add("less_equal_" + typeName, new function_execution<details::less_equal<T> >());
			fList.add("less_equal_" + typeName, 2);
			fMap.add("greater_" + typeName, new function_execution<details::greater<T> >());
			fList.add("greater_" + typeName, 2);
			fMap.add("greater_equal" + typeName, new function_execution<details::greater_equal<T> >());
			fList.add("greater_equal_" + typeName, 2);
		}
	}
}

#endif /* HYPER_MODEL_OPERATOR_HH_ */