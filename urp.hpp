/* Some fun with Reactive Programming in C++17.
 *
 * Copyright 2019 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 * See https://github.com/joaquintides/usingstdcpp2019 for talk material.
 */

#ifndef USINGSTDCPP2019_URP_HPP
#define USINGSTDCPP2019_URP_HPP

#if defined(_MSC_VER)
#pragma once
#endif

#include <array>
#include <boost/signals2/signal.hpp>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace usingstdcpp2019::urp{

namespace detail{

template<typename... Ts> struct overloaded:Ts...{using Ts::operator()...;};
template<typename... Ts> overloaded(Ts...)->overloaded<Ts...>;

template<std::size_t I>
using node_index_type=std::integral_constant<std::size_t,I>;

template<typename Derived,typename Signature,typename... Srcs>
class node;

template<typename Derived,typename Signature,typename... Srcs>
void swap(
  node<Derived,Signature,Srcs...>& x,node<Derived,Signature,Srcs...>& y)
{
  x.swap(y);
}

template<typename Derived,typename... SigArgs>
class node<Derived,void(SigArgs...)>
{
public:
  node()=default;
  node(const node&){};
  node(node&& x):sig{std::move(x.sig)}{sig(this);}
    
  node& operator=(const node&){return *this;}
  node& operator=(node&& x)
  {
    if(this!=&x){
      sig=std::move(x.sig);
      sig(this);
    }
    return *this;
  }
   
  void swap(node& x)
  {
    if(this!=&x){
      sig.swap(x.sig);
      sig(this);
      x.sig(&x);
    }
  }

  template<typename Slot>
  auto connect(const Slot& s)
  {
    return connect_node([=](auto arg){
      std::visit(overloaded{
        [](node*){},
        [=](auto& sigargs){std::apply(s,sigargs);}
      },arg);
    });
  }

protected:
  void signal(SigArgs... sigargs)
  {
    sig(std::forward_as_tuple(std::forward<SigArgs>(sigargs)...));
  }

  auto get_srcs()const noexcept{return std::tuple{};}
    
private:
  template<typename,typename,typename...> friend class node;

  using extended_signature=void(std::variant<node*,std::tuple<SigArgs...>>);

  template<typename Slot>
  auto connect_node(const Slot& s){return sig.connect(s);}

  boost::signals2::signal<extended_signature> sig;
};

template<typename Derived,typename... SigArgs,typename... Srcs>
class node<Derived,void(SigArgs...),Srcs...>:
  public node<Derived,void(SigArgs...)>
{
  using super=node<Derived,void(SigArgs...)>;

public:
  node(Srcs&... srcs):srcs{&srcs...}{}
  node(const node& x):super{x},srcs{x.srcs}{}
  node(node&& x):super{std::move(x)},srcs{x.srcs}{x.disconnect_srcs();}
  template<typename Derived2,typename Signature2>
  explicit node(node<Derived2,Signature2,Srcs...>&& x):
    srcs{x.srcs}{x.disconnect_srcs();} 
  template<
    typename Derived2,typename Signature2,typename... Srcs2,
    typename Derived3,typename Signature3,typename... Srcs3,
    std::enable_if_t<
      std::is_same_v<void(Srcs...),void(Srcs2...,Srcs3...)>
    >* =nullptr
  >
  node(
    node<Derived2,Signature2,Srcs2...>&& x,
    node<Derived3,Signature3,Srcs3...>&& y):
    srcs{std::tuple_cat(x.srcs,y.srcs)}
    {x.disconnect_srcs();y.disconnect_srcs();}
  ~node(){disconnect_srcs();}
    
  node& operator=(const node& x)
  {
    if(this!=&x){
      base()=x;
      srcs=x.srcs;
      disconnect_srcs();
      conns=connect_srcs();
    }
    return *this;
  }

  node& operator=(node&& x)
  {
    if(this!=&x){
      base()=std::move(x);
      srcs=x.srcs;
      disconnect_srcs();
      conns=connect_srcs();
      x.disconnect_srcs();
    }
    return *this;
  }
   
  void swap(node& x)
  {
    if(this!=&x){
      base().swap(x.base());
      std::swap(srcs,x.srcs);
      disconnect_srcs();
      conns=connect_srcs();
      x.disconnect_srcs();
      x.conns=x.connect_srcs();
    }
  }

protected:
  auto& get_srcs()const noexcept{return srcs;}

private:
  template<typename,typename,typename...> friend class node;

  super&   base()noexcept{return *this;}
  Derived& derived()noexcept{return static_cast<Derived&>(*this);}
 
  auto connect_srcs()
  {
    return connect_srcs(std::make_index_sequence<sizeof...(Srcs)>{});
  }

#if defined(_MSC_VER)

  template<std::size_t I>
  struct slot
  {
    template<typename Arg>
    void operator()(Arg arg)const{
      std::visit(overloaded{
        [this](auto* p){
          auto& src=std::get<I>(this_->srcs);
          src=static_cast<std::decay_t<decltype(src)>>(p);
        },
        [this](auto& sigargs){
          std::apply([this](auto&&... sigargs){
            this_->derived().callback(
              node_index_type<I>{},
              std::forward<decltype(sigargs)>(sigargs)...);
          },sigargs);
        }
      },arg);
    }

    node* this_;
  };

  template<std::size_t> friend struct slot;

  template<std::size_t... I>
  auto connect_srcs(std::index_sequence<I...>)
  {
    return std::array{std::get<I>(srcs)->connect_node(slot<I>{this})...};
  }

#else

  template<std::size_t... I>
  auto connect_srcs(std::index_sequence<I...>)
  {
    return std::array{
      std::get<I>(srcs)->connect_node([this](auto arg){
        std::visit(overloaded{
          [this](auto* p){
            auto& src=std::get<I>(srcs);
            src=static_cast<std::decay_t<decltype(src)>>(p);
          },
          [this](auto& sigargs){
            std::apply([this](auto&&... sigargs){
              derived().callback(
                node_index_type<I>{},
                std::forward<decltype(sigargs)>(sigargs)...);
            },sigargs);
          }
        },arg);
      })...
    };
  }

#endif

  void disconnect_srcs()
  {
    std::apply([](auto&&... conns){(conns.disconnect(),...);},conns);
  }

  std::tuple<Srcs*...>                                    srcs;
  std::array<boost::signals2::connection,sizeof...(Srcs)> conns=connect_srcs();
};

} /* namespace detail */

template<typename F,typename... Args> class function;

template<typename T>
class value:public detail::node<value<T>,void(const value<T>&)>
{
  using super=detail::node<value,void(const value&)>;

public:
  using value_type=T;

  value(const T& t):t{t}{}
  value(const value&)=default;
  value(value&&)=default;
    
  value& operator=(const value& x)
  {
    if(this!=&x){
      base()=x;
      *this=x.t;
    }
    return *this;
  }
  
  value& operator=(value&& x)=default;

  value& operator=(const T& u)
  {
    if(!(t==u)){
      t=u;
      this->signal(*this);
    }
    return *this;
  }

  value& operator=(T&& u)
  {
    if(!(t==u)){
      t=std::move(u);
      this->signal(*this);
    }
    return *this;
  }

  void swap(value& x)
  {
    using std::swap;
    base().swap(x.base());
    swap(t,x.t);
  }

  const T& get()const noexcept{return t;}
  
  template<typename F>
  auto operator|(F f)&{return function{f,*this};}
    
private:
  super& base()noexcept{return *this;}

  T t;
};

template<typename T>
void swap(value<T>& x,value<T>& y){x.swap(y);}

namespace detail{

template<typename F1,typename F2>
auto compose_function(F1 f1,F2 f2)
{
  return[=](auto&&... x){
    return f1(f2(std::forward<decltype(x)>(x)...));};
}

template<std::size_t I0,typename Tuple,std::size_t... I>
auto subtuple(Tuple&& t,std::index_sequence<I...>)
{
  return std::forward_as_tuple(std::get<I0+I>(std::forward<Tuple>(t))...);
}

template<std::size_t I0,std::size_t Length,typename Tuple>
auto subtuple(Tuple&& t)
{
  return detail::subtuple<I0>(
    std::forward<Tuple>(t),std::make_index_sequence<Length>{});
}

template<
  std::size_t Arity2,std::size_t Arity3,
  typename F1,typename F2,typename F3
>
auto compose_function(F1 f1,F2 f2,F3 f3)
{
  return[=](auto&&... x){
    auto t=std::forward_as_tuple(std::forward<decltype(x)>(x)...);
    auto t2=detail::subtuple<0,Arity2>(std::move(t));
    auto t3=detail::subtuple<Arity2,Arity3>(std::move(t));
    return f1(std::apply(f2,t2),std::apply(f3,t3));
  };
}

struct identity_f
{
  template<typename T>
  auto operator()(const T& x)const{return x;}
};

template<typename Arg>
auto identity_function(Arg& arg)
{
  return function{identity_f{},arg};
}

} /* namespace detail */

template<typename F,typename... Args>
class function:
  public detail::node<
    function<F,Args...>,void(const function<F,Args...>&),Args...
  >
{
  using super=detail::node<function,void(const function&),Args...>;

public:
  using value_type=decltype(std::declval<F>()(std::declval<Args>().get()...));
    
  function(F f,Args&... args):super{args...},f{f}{}
  function(const function& x)=default;
  function(function&& x)=default;
  template<typename F1,typename F2>
  function(F1 f1,function<F2,Args...>&& x):
    super{std::move(x)},f{detail::compose_function(f1,x.f)}{}
  template<
    typename F1,typename F2,typename... Args2,typename F3,typename... Args3
  >
  function(F1 f1,function<F2,Args2...>&& x,function<F3,Args3...>&& y):
    super{std::move(x.base()),std::move(y.base())},
    f{detail::compose_function<sizeof...(Args2),sizeof...(Args3)>(
      f1,x.f,y.f)
    }{}
  template<typename F1,typename F2,typename... Args2,typename Arg3>
  function(F1 f1,function<F2,Args2...>&& x,Arg3& y):
    function{f1,std::move(x),detail::identity_function(y)}{}
  template<typename F1,typename Arg2,typename F3,typename... Args3>
  function(F1 f1,Arg2& x,function<F3,Args3...>&& y):
    function{f1,detail::identity_function(x),std::move(y)}{}
    
  function& operator=(const function& x)
  {
    if(this!=&x){
      base()=x;
      f=x.f;
      update();
    }
    return *this;
  }

  function& operator=(function&& x)=default;

  void swap(function& x)
  {
    using std::swap;
    base().swap(x.base());
    swap(f,x.f);
    swap(t,x.t);
  }

  auto const& get()const noexcept{return t;}
    
  template<typename G>
  auto operator|(G g)& {return urp::function{g,*this};}
  template<typename G>
  auto operator|(G g)&&{return urp::function{g,std::move(*this)};}

private:
  friend super;
  template<typename,typename...> friend class function;

  super& base()noexcept{return *this;}

  template<typename Index,typename Arg>
  void callback(Index,const Arg&){update();}
  
  auto value()const
  {
    return std::apply([this](const auto&... args){
      return f(args->get()...);},this->get_srcs());
  }

  void update()
  {
    if(const auto& u=value();!(t==u)){
      t=u;
      this->signal(*this);
    }
  }
  
  F          f;
  value_type t=value();
};

template<typename F1,typename F2,typename... Args>
function(F1,function<F2,Args...>&&)->function<
  decltype(detail::compose_function(std::declval<F1>(),std::declval<F2>())),
  Args...
>;

template<
  typename F1,typename F2,typename... Args2,typename F3,typename... Args3
>
function(F1,function<F2,Args2...>&&,function<F3,Args3...>&&)->function<
  decltype(
    detail::compose_function<sizeof...(Args2),sizeof...(Args3)>(
      std::declval<F1>(),std::declval<F2>(),std::declval<F3>())),
  Args2...,Args3...
>;

template<typename F1,typename F2,typename... Args2,typename Arg3>
function(F1,function<F2,Args2...>&&,Arg3&)->function<
  decltype(
    detail::compose_function<sizeof...(Args2),1>(
      std::declval<F1>(),std::declval<F2>(),detail::identity_f{})),
  Args2...,Arg3
>;

template<typename F1,typename Arg2,typename F3,typename... Args3>
function(F1 f1,Arg2& x,function<F3,Args3...>&& y)->function<
  decltype(
    detail::compose_function<1,sizeof...(Args3)>(
      std::declval<F1>(),detail::identity_f{},std::declval<F3>())),
  Arg2,Args3...
>;

template<typename F,typename... Args>
void swap(function<F,Args...>& x,function<F,Args...>& y){x.swap(y);}

namespace detail{

template<typename T>
struct is_function_or_value_impl:std::false_type{};
template<typename T>
struct is_function_or_value_impl<value<T>>:std::true_type{};
template<typename F,typename... Args>
struct is_function_or_value_impl<function<F,Args...>>:std::true_type{};
template<typename T>
inline constexpr bool is_function_or_value=
  is_function_or_value_impl<std::decay_t<T>>::value;

} /* namespace detail */

#define USINGSTDCPP2019_URP_DEFINE_UNARY_OP(name)             \
template<                                                     \
  typename T,                                                 \
  std::enable_if_t<detail::is_function_or_value<T>>* =nullptr \
>                                                             \
auto operator name(T&& x)                                     \
{                                                             \
  return function{                                            \
    [](const auto& x){return name x;},                        \
    std::forward<T>(x)                                        \
  };                                                          \
}

#define USINGSTDCPP2019_URP_DEFINE_BINARY_OP(name)            \
template<                                                     \
  typename T,typename U,                                      \
  std::enable_if_t<                                           \
    !detail::is_function_or_value<T>&&                        \
    detail::is_function_or_value<U>                           \
  >* =nullptr                                                 \
>                                                             \
auto operator name(T&& x,U&& y)                               \
{                                                             \
  return function{                                            \
    [x=std::forward<T>(x)](const auto& y){return x name y;},  \
    std::forward<U>(y)                                        \
  };                                                          \
}                                                             \
                                                              \
template<                                                     \
  typename T,typename U,                                      \
  std::enable_if_t<                                           \
    detail::is_function_or_value<T>&&                         \
    !detail::is_function_or_value<U>                          \
  >* =nullptr                                                 \
>                                                             \
auto operator name(T&& x,U&& y)                               \
{                                                             \
  return function{                                            \
    [y=std::forward<U>(y)](const auto& x){return x name y;},  \
    std::forward<T>(x)                                        \
  };                                                          \
}                                                             \
                                                              \
template<                                                     \
  typename T,typename U,                                      \
  std::enable_if_t<                                           \
    detail::is_function_or_value<T>&&                         \
    detail::is_function_or_value<U>                           \
  >* =nullptr                                                 \
>                                                             \
auto operator name(T&& x,U&& y)                               \
{                                                             \
  return function{                                            \
    [](const auto& x,const auto& y){return x name y;},        \
    std::forward<T>(x),std::forward<U>(y)                     \
  };                                                          \
}

USINGSTDCPP2019_URP_DEFINE_UNARY_OP(+)
USINGSTDCPP2019_URP_DEFINE_UNARY_OP(-)
USINGSTDCPP2019_URP_DEFINE_BINARY_OP(+)
USINGSTDCPP2019_URP_DEFINE_BINARY_OP(-)
USINGSTDCPP2019_URP_DEFINE_BINARY_OP(*)
USINGSTDCPP2019_URP_DEFINE_BINARY_OP(/)

#undef USINGSTDCPP2019_URP_DEFINE_BINARY_OP
#undef USINGSTDCPP2019_URP_DEFINE_UNARY_OP

template<typename F,typename... Srcs> class event;

template<typename T>
class trigger:public detail::node<trigger<T>,void(const trigger<T>&,const T&)>
{
  using super=detail::node<trigger,void(const trigger&,const T&)>;

public:
  using value_type=T;

  using super::super;
    
  trigger& operator=(const trigger& x)=default;
  trigger& operator=(trigger&& x)=default;

  trigger& operator=(const T& t)
  {
    this->super::signal(*this,t);
    return *this;
  }

  template<typename Slot>
  auto operator|(Slot s)&{return event{s,*this};}
};

template<typename T>
void swap(trigger<T>& x,trigger<T>& y){return x.swap(y);}

namespace detail{

template<typename ValueFunction,typename F>
class action
{
public:
  action(ValueFunction,F f):f{f}{}

  template<typename... Args>
  static auto value(const Args&... args)->
  decltype(std::declval<ValueFunction>()(args...));

  template<typename... Args>
  void operator()(Args&&... args){f(std::forward<Args>(args)...);}

private:
  F f;
};

inline constexpr auto type_passthrough=[](const auto& x,const auto&... xs){
  return std::common_type_t<
    std::decay_t<decltype(x)>,std::decay_t<decltype(xs)>...>(x);
};

template<typename Action1,typename Action2>
struct compose_action_value_function
{
  template<typename... Args>
  auto operator()(const Args&... args)->
  decltype(Action1::value(Action2::value(args...)));
};

template<typename Action1,typename Action2>
auto compose_action(Action1 act1,Action2 act2)
{
  return action{
    compose_action_value_function<Action1,Action2>{},
    [=](auto& sig,auto index,const auto& x)mutable{
      auto sig2=[&](const auto& y){act1(sig,node_index_type<0>{},y);};
      act2(sig2,index,x);
    }
  };
}

template<typename Action,typename... Srcs>
using event_value_type=decltype(
  Action::value(std::declval<const typename Srcs::value_type&>()...));

} /* namespace detail */

template<typename Action,typename... Srcs>
class event:public detail::node<
  event<Action,Srcs...>,
  void(
    const event<Action,Srcs...>&,
    const detail::event_value_type<Action,Srcs...>&
  ),
  Srcs...
>
{
  using super=detail::node<
    event,
    void(const event&,const detail::event_value_type<Action,Srcs...>&),
    Srcs...
  >;

public:
  using value_type=detail::event_value_type<Action,Srcs...>;
    
  event(Action act,Srcs&... srcs):super{srcs...},act{act}{}
  event(const event& x)=default;
  event(event&& x)=default;
  template<typename Action1,typename Action2>
  event(Action1 act1,event<Action2,Srcs...>&& x):
    super{std::move(x)},act{detail::compose_action(act1,x.act)}{}

  event& operator=(const event&)=default;
  event& operator=(event&&)=default;

  void swap(event& x)
  {
    using std::swap;
    base().swap(x.base());
    swap(act,x.act);
  }

  template<typename Action2>
  auto operator|(Action2 act2)& {return urp::event{act2,*this};}
  template<typename Action2>
  auto operator|(Action2 act2)&&
    {return urp::event{std::move(act2),std::move(*this)};}
    
private:
  friend super;
  template<typename,typename...> friend class event;

  super& base()noexcept{return *this;}

  template<typename Index,typename Src,typename T>
  void callback(Index index,Src&,const T& x)
  {
    auto sig=[this](const value_type& y){this->signal(*this,y);};
    act(sig,index,x);
  }

  Action act;
};

template<typename Action1,typename Action2,typename... Srcs>
event(Action1,event<Action2,Srcs...>&&)->event<
  decltype(
    detail::compose_action(std::declval<Action1>(),std::declval<Action2>())),
  Srcs...
>;

template<typename Action,typename... Srcs>
void swap(event<Action,Srcs...>& x,event<Action,Srcs...>& y){x.swap(y);}

template<typename Pred>
auto filter(Pred pred)
{
  return detail::action{
    detail::type_passthrough,
    [=](auto& sig,auto,const auto& x){if(pred(x))sig(x);}
  };
}

template<typename F>
auto map(F f)
{
  return detail::action{
    [=](const auto& x,const auto&... xs){
      return std::common_type_t<
        std::decay_t<decltype(f(x))>,std::decay_t<decltype(f(xs))>...>(f(x));
    },
    [=](auto& sig,auto,const auto& x){sig(f(x));}
  };
}

template<typename... Srcs>
auto merge(Srcs&... srcs)
{
  return event{
    detail::action{
      detail::type_passthrough,
      [](auto& sig,auto,const auto& x){sig(x);}
    },
    srcs...
  };
}

template<typename T,typename BynaryOp>
auto accumulate(T init,BynaryOp op)
{
  return detail::action{
    [=](const auto&...){return std::move(init);},
    [=,res=std::move(init)](auto& sig,auto,const auto& x)mutable{
      res=op(std::move(res),x);
      sig(res);
    }
  };
}

template<typename... Srcs>
auto combine(Srcs&... srcs)
{
  using cache_type=std::tuple<std::optional<typename Srcs::value_type>...>;

  return event{
    detail::action{
      [](const auto&... xs){return std::make_tuple(xs...);},
      [os=cache_type{},remaining=sizeof...(Srcs)]
      (auto& sig,auto index,const auto& x)mutable{
        auto& o=std::get<index.value>(os);
        if(!o)--remaining;
        o=x;
        if(!remaining){
          sig(std::apply([](auto&&... os){
            return std::make_tuple(std::move(*os)...);
          },os));
          os=cache_type{};
          remaining=sizeof...(Srcs);
        }
      }
    },
    srcs...
  };
}

} /* namespace usingstdcpp2019::urp */

#endif
