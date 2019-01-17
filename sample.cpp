/* Copyright 2019 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 * See https://github.com/joaquintides/usingstdcpp2019 for talk material.
 */
 
#include <iostream>
#include "urp.hpp"

int main()
{
  using namespace usingstdcpp2019::urp;
    
  value x=0,y=0;
  auto  z=(x*x)+(y*y)+1;
  
  x=4;
  y=5;
  std::cout<<z.get()<<"\n";
    
  trigger<int> t;
  auto         e=t|filter([](const auto&x){return x%2==0;});
    
  e.connect([](auto,const auto&x){std::cout<<x<<"\n";});
    
  t=1;t=2;t=3;t=4;
}