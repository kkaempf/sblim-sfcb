require 'test/unit'
require 'net/http'
require 'uri'
require File.expand_path(File.dirname(__FILE__), "sfcb.rb")

class StartStopTest < Test::Unit::TestCase
  def setup
    @sfcb = Sfcb.new
    @sfcb.start
    sleep 3
    puts "\n\t*** Started"
    @http = Net::HTTP.new "localhost", "#{@sfcb.port}"
  end
  def teardown
    puts "\n\t*** Stopping"
    @sfcb.stop
  end
  
  def test_get_root
    resp = @http.get "/cimrs/"
    assert resp
  end
end
