// RUN: %clang_verify %show_variables %s

void f1() {
}

void f2(int k) { // expected-note {{variable 'k' declared here}}
}

void f3(int);
void f3(int k) { // expected-note {{variable 'k' declared here}}
}

void f4(int j);
void f4(int k) { // expected-note {{variable 'k' declared here}}
}
