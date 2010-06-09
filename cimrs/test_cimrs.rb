require 'test/unit'
require 'net/http'
require 'uri'
require File.join(File.dirname(__FILE__), "sfcb.rb")

class StartStopTest < Test::Unit::TestCase
  def setup
    @sfcb = Sfcb.new
#    @sfcb.start
#    sleep 3
#    puts "\n\t*** Started"
    @http = Net::HTTP.start "localhost", "#{@sfcb.port}"
  end
  def teardown
#    puts "\n\t*** Stopping"
#    @sfcb.stop
    @http.finish
  end
  
  def test_get_root

    resp = @http.get "/cimrs/"
    assert_equal "404", resp.code
  end

  def test_get_namespaces
    resp = @http.get "/cimrs/namespaces"
    assert_equal "200", resp.code
  end
  
  def test_put_namespaces
    resp = @http.put "/cimrs/namespaces", "foo"
    assert_equal "405", resp.code
    assert_equal "GET", resp["Allowed"]
  end
#    resp = @http.get "/cimrs/namespaces"
#    resp = @http.post "/cimrs/namespaces"
#    resp = @http.delete "/cimrs/namespaces"
#    assert resp
#  end

#  def test_get_namespace_root_cimv2
#    resp = @http.get "/cimrs/namespaces/root%2Fcimv2"
#    assert resp
#  end

#  def test_get_classes_root_cimv2
#    resp = @http.get "/cimrs/namespaces/root%2Fcimv2/classes"
#    assert resp
#  end
end
