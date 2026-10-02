use Cro::HTTP::Router;
use Cro::HTTP::Server;

my $application = route {
    get -> {
        content 'text/plain', "Hello from Cro!\n";
    }
    get -> 'greet', $name {
        content 'text/plain', "Hello, $name!\n";
    }
}

my $host = %*ENV<HELLO_HTTP_HOST> // 'localhost';
my $port = %*ENV<HELLO_HTTP_PORT> // 10000;

my $http = Cro::HTTP::Server.new(:$host, :$port, :$application);
$http.start;
say "Listening at http://$host:$port";

react whenever signal(SIGINT) {
    $http.stop;
    done;
}
