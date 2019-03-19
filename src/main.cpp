#include <iostream>

using namespace std;


auto print(auto &data){
  cout<<data<<endl;
}

int main(int argc, char **argv){
    print("test");
}
