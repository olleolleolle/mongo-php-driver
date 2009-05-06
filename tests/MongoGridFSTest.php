<?php
require_once 'PHPUnit/Framework.php';

require_once 'Mongo/GridFS.php';

/**
 * Test class for Mongo.
 * Generated by PHPUnit on 2009-04-09 at 18:09:02.
 */
class MongoGridFSTest extends PHPUnit_Framework_TestCase
{
    /**
     * @var    MongoCursor
     * @access protected
     */
    protected $object;

    /**
     * Sets up the fixture, for example, opens a network connection.
     * This method is called before a test is executed.
     *
     * @access protected
     */
    protected function setUp()
    {
        $db = $this->sharedFixture->selectDB('phpunit');
        $this->object = $db->getGridFS();
        $this->object->drop();
        $this->object->start = memory_get_usage(true);
    }

    protected function tearDown() {
        $this->assertEquals($this->object->start, memory_get_usage(true));
    }


    public function testDrop() {
        $this->object->storeFile('./somefile');
        $c = $this->object->chunks->count();
        $this->assertGreaterThan(0, $c);
        $this->assertEquals($this->count(), 1);

        $this->object->drop();

        $this->assertEquals($this->object->chunks->count(), 0);
        $this->assertEquals($this->object->count(), 0);
    }

    public function testFind() {
        $this->object->storeFile('./somefile');

        $cursor = $this->object->find();
        $this->assertTrue($cursor instanceof MongoGridFSCursor);
        $file = $cursor->getNext();
        $this->assertTrue($file instanceof MongoGridFSFile);
    }

    public function testStoreFile() {
        $this->assertEquals($this->object->findOne(), null);
        $this->object->storeFile('./somefile');
        $this->assertNotNull($this->object->findOne());
        $this->assertNotNull($this->object->chunks->findOne());
    }

    public function testFindOne() {
        $this->assertEquals($this->object->findOne(), null);
        $this->object->storeFile('./somefile');
        $obj = $this->object->findOne();

        $this->assertTrue($obj instanceof MongoGridFSFile);

        $obj = $this->object->findOne(array('filename' => 'zxvf'));
        $this->assertEquals($obj, null);

        $obj = $this->object->findOne('./somefile');
        $this->assertNotNull($obj);
    }

    public function testRemove()
    {
        $this->object->storeFile('./somefile');

        $this->object->remove();
        $this->assertEquals($this->object->findOne(), null);
        $this->assertEquals($this->object->chunks->findOne(), null);
    }

    public function testBasic()
    {
        mongo_gridfs_store($this->object->resource, "./somefile");
        $this->assertNotNull($this->object->findOne());
    }

    public function testStoreUpload() {
        $_FILES['x']['name'] = 'myfile';
        $_FILES['x']['tmp_name'] = 'somefile';
      
        $this->object->storeUpload('x');

        $file = $this->object->findOne();
        $this->assertTrue($file instanceof MongoGridFSFile);
        $this->assertEquals($file->getFilename(), 'myfile');

        $this->object->drop();

        $this->object->storeUpload('x', 'y');

        $file = $this->object->findOne();
        $this->assertTrue($file instanceof MongoGridFSFile);
        $this->assertEquals($file->getFilename(), 'y');
    }

    public function getBytes() {
        $contents = file_get_contents('somefile');

        $this->object->storeFile('somefile');
        $file = $this->object->findOne();

        $this->assertEquals($file->getBytes(), $contents);
    }
}
?>
